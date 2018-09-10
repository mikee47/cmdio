/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include <status.h>
#include "Services/IFS/HybridFileSystem.h"
#include "Services/IFS/IFSFlashMedia.h"
#include "Services/IFS/FWObjectStore.h"
#include "../Services/SpifFS/spiffs_sming.h"
#include "FileManager.h"

// For testing
//#define FWFS_ONLY


DEFINE_STRING_P(ATTR_ACCESS, "access")

static DEFINE_STRING_P(METHOD_FILES, "files")

// LIST
static DEFINE_STRING_P(COMMAND_LIST, "list")
static DEFINE_STRING_P(ATTR_FILES, "files")
static DEFINE_STRING_P(ATTR_DIR, "dir")
// GET
static DEFINE_STRING_P(COMMAND_GET, "get")
// UPLOAD
static DEFINE_STRING_P(COMMAND_UPLOAD, "upload")
// DELETE
static DEFINE_STRING_P(COMMAND_DELETE, "delete")
// STAT/UPLOAD
static DEFINE_STRING_P(ATTR_SIZE, "size")
static DEFINE_STRING_P(ATTR_ATTR, "attr")
static DEFINE_STRING_P(ATTR_MTIME, "mtime")
static DEFINE_STRING_P(ATTR_WRITTEN, "written")
// INFO
static DEFINE_STRING_P(ATTR_VOLUME_SIZE, "volumesize")
static DEFINE_STRING_P(ATTR_FREE_SPACE, "freespace")
// CHECK
static DEFINE_STRING_P(COMMAND_CHECK, "check")
// FORMAT
static DEFINE_STRING_P(COMMAND_FORMAT, "format")

// This is DWORD aligned so we can access it directly
extern const uint8_t __fwfiles_data[] PROGMEM;

#define UPLOAD_TIMEOUT_MS 2000

static void getFileInfo(JsonObject& json, const FileStat& stat)
{
	if (!json.containsKey(ATTR_NAME())) {
		// Needs the cast to make Json create a copy of the string
		json[ATTR_NAME()] = stat.name.length ? String(stat.name) : "";
	}
	json[ATTR_SIZE()] = stat.size;
	char buf[10];
	fileAclToStr(stat.acl, buf, sizeof(buf));
	json[ATTR_ACCESS()] = String(buf);	// ArduinoJson bug, doesn't copy char* as it should
	fileAttrToStr(stat.attr, buf, sizeof(buf));
	json[ATTR_ATTR()] = String(buf);
	json[ATTR_MTIME()] = stat.mtime;
}

static void getFileInfo(JsonObject& json, file_t file)
{
	FileNameStat stat;
	if (fileStats(file, &stat) >= 0)
		getFileInfo(json, stat);
}

bool FileUpload::init(const char* filename, size_t size)
{
	m_filename = filename;
	m_size = size;
	m_file = fileOpen(m_filename, eFO_CreateNewAlways | eFO_WriteOnly);
	debug_i("fileOpen('%s'): %d", filename, m_file);
	if (m_file < 0)
		return false;

	m_error = ERROR_TIMEOUT;
	m_timer.setCallback([](void* arg) {
		reinterpret_cast<FileUpload*>(arg)->endUpload();
	}, this);
	m_timer.startMs(UPLOAD_TIMEOUT_MS);

	return true;
}

void FileUpload::close()
{
	if (m_file >= 0) {
		fileClose(m_file);
		m_file = -1;
	}
}

bool FileUpload::handleData(command_connection_t connection, uint8_t* data, size_t size)
{
	if (m_connection != connection)
		return false;

	debug_i("%s(%u)", __FUNCTION__, size);

	m_timer.stop();

	int n = fileWrite(m_file, data, size);
	if (n != (int)size) {
		debug_e("File write error");
		m_error = n;
	}
	else {
		m_written += size;
		// Need more data
		if (m_written < m_size) {
			m_timer.startMs(UPLOAD_TIMEOUT_MS);
			return true;
		}

		m_error = (m_written == m_size) ? FS_OK : ERROR_TOO_BIG;
	}

	endUpload();
	return true;
}

void FileUpload::endUpload()
{
	if (m_file >= 0)
		fileFlush(m_file);

	if (m_connection) {
		DynamicJsonBuffer buffer;
		JsonObject& json = buffer.createObject();
		json[ATTR_METHOD()] = METHOD_FILES();
		json[ATTR_COMMAND()] = COMMAND_UPLOAD();
		json[ATTR_WRITTEN()] = m_written;
		getFileInfo(json, m_file);
		if (m_error)
			setError(json, m_error, fileGetErrorString(m_error));
		else
			setSuccess(json);
		m_connection->send(json);
	}
	close();

	m_manager.endUpload();
}

/* CFileManager */

FileManager::~FileManager()
{
	endUpload();
}

bool FileManager::init()
{
	fileFreeFileSystem();

	auto freeheap = system_get_free_heap_size();
	debug_i("1: heap = %u", freeheap);

	auto fwMedia = new IFSFlashMedia(__fwfiles_data, eFMA_ReadOnly);
	debug_i("2: heap = -%u", freeheap - system_get_free_heap_size());
	auto store = new FWObjectStore(fwMedia);
	debug_i("3: heap = -%u", freeheap - system_get_free_heap_size());
#ifdef FWFS_ONLY
	auto fs = new FirmwareFileSystem(store);
#else
	auto cfg = spiffs_get_storage_config();
	auto ffsMedia = new IFSFlashMedia(cfg.phys_addr, cfg.phys_size, eFMA_ReadWrite);
	debug_i("4: heap = -%u", freeheap - system_get_free_heap_size());
	auto fs = new HybridFileSystem(store, ffsMedia);
#endif
	debug_i("5: heap = -%u", freeheap - system_get_free_heap_size());
	if (!fs)
		return false;

	int res = fs->mount();

	debug_i("6: heap = -%u", freeheap - system_get_free_heap_size());

	char buf[20];
	fs->geterrortext(res, buf, sizeof(buf));
	debug_i("mount() returned %d (%s)", res, buf);

	if (res < 0) {
		delete fs;
		return false;
	}

	fileSetFileSystem(fs);
	return true;
}

void FileManager::endUpload()
{
	if (m_upload) {
		if (m_callback)
			m_callback(*m_upload);
		delete m_upload;
		m_upload = nullptr;
	}
}

static JsonObject& findOrCreateFile(JsonArray& files, const String& name)
{
	for (auto& f : files)
		if (name == f[ATTR_NAME()])
			return f;

	auto& f = files.createNestedObject();
	f[ATTR_NAME()] = name;
	return f;
}


/* @todo create an IDataSourceStream for file listings.
 * We only need to buffer one file entry.
 * We can still use JSON for each entry, but we'll manually generate
 * opening/closing stuff. The 'opening' stuff will be copied from
 * the request so it gets emitted first.
 *
 * 1. Create the stream object
 * 2. Add the json content to the stream
 * 3. Set json[DONT_RESPOND()] = true
 * 4. Call connection->send(stream)
 *
 * OK, so there's a bit of a problem with all this: WebSocketConnection
 * buffers everything anyway and doesn't support streams.
 *
 * Need to take a proper look at using HTTP (e.g. REST) as a command
 * interface. We'd still use websockets, but for all file-related stuff
 * HTTP might be a better fit. There's still the issue with antivirus
 * software adding scripts to HTML; maybe there's a way to defeat that by
 * tagging the transfer in some way. Probably not.
 *
 */
static void listFiles(JsonObject& json)
{
	auto& files = json.createNestedArray(ATTR_FILES());

	filedir_t dir;
	int res = fileOpenDir(json[ATTR_DIR()].asString(), &dir);
	if (res >= 0) {
		FileNameStat stat;
		while ((res = fileReadDir(dir, &stat)) >= 0) {
			JsonObject& file = findOrCreateFile(files, stat.name.buffer);
			getFileInfo(file, stat);
		}
		fileCloseDir(dir);
	}

	if (res == FS_OK || res == FSERR_NoMoreFiles)
		setSuccess(json);
	else
		setError(json, res, fileGetErrorString(res));
}

static void deleteFiles(JsonObject& json)
{
	int res = FS_OK;
	String dir = json[ATTR_DIR()].asString();
	JsonArray& files = json[ATTR_FILES()];
	for (unsigned i = 0; i < files.size(); ++i) {
		JsonObject& file = files[i];
		String path = dir + "/" + file[ATTR_NAME()].asString();
		int err = fileDelete(path);
		if (err < 0) {
			setError(file, err, fileGetErrorString(err));
			if (res == FS_OK)
				res = err;
		}
		else
			setSuccess(file);
	}

	if (res == FS_OK)
		setSuccess(json);
	else
		setError(json, res, fileGetErrorString(res));
}

static void getInfo(JsonObject& json)
{
	FileSystemInfo info;
	int err = fileGetSystemInfo(info);
	if (err) {
		setError(json, err, fileGetErrorString(err));
		return;
	}
	json[ATTR_VOLUME_SIZE()] = info.volumeSize;
	json[ATTR_FREE_SPACE()] = info.freeSpace;
	setSuccess(json);
}

static void check(JsonObject& json)
{
	int err = fileSystemCheck();
	if (err)
		setError(json, err, fileGetErrorString(err));
	else
		setSuccess(json);
}

/*
 * Reformat SPIFFS to blank state
 */
static void format(JsonObject& json)
{
	int res = fileSystemFormat();
	if (res < 0)
		setError(json, res);
	else
		setSuccess(json);
}

String FileManager::getMethod() const
{
	return METHOD_FILES();
}

/*
 * TODO:
 *
 * Get a file using a websocket. This can only be used for small files but is
 * authenticated so can be used to obtain the contents of protected config files.
 *
 * For small config files we could integrate the JSON into the respopnse but this
 * is inefficient and restricts file sizes further. Sending the file via binary
 * connection is better but should probably have a header.
 *
 * It would probably be easier to authenticate HTTP connections using tokens.
 * This would be generated and returned in the websocket authentication
 * packet. Each websocket connection would need to remember it. If an HTTP
 * request requires authentication then the token would require matching
 * against all open websockets for a match; that would determine access level.
 *
 * The token could be as simple as the 32-bit connection ID, however it would
 * be wise to hash it for additional security. We have ghash available, but
 * as it's non-reversible we'd either need to compute on the fly or cache
 * at the expense of extra RAM. Without extra entropy the token can be too easily
 * created from the CID so it looks like we need to throw in some additional
 * random data before hashing.
 *
 * Note that we impose the condition that public connections are via HTTPS
 * to protect session data.
 *
 * To summarise: The purpose of a CID is to ensure only one websocket is kept
 * open per client browser, which is an optimisation/convenience function.
 * The purpose of a token is to enforce authentication, so must be secure.
 *
 * Token can be passed in URL, or as cookie but probably best as custom header
 * value.
 *
 */
ioerror_t FileManager::getFile(command_connection_t connection, JsonObject& json)
{
	return setError(json, ioe_not_impl);
	/*

	 const char* name = json[ATTR_NAME()];
	 if (!name) {
		 setError(json, ioe_bad_param);
		 return;
	 }

	 file_t fh = fileOpen(filename, eFO_ReadOnly);
	 if (fh < 0) {
	 	 return ioe_spiffs;

	 }

	 filestream_t fs = openFile(filename);
	 if (!fs)
	 	 return;

	 wsFrameType ft = WS_BINARY_FRAME;
	 bool fin = false;
	 do {
	 char buffer[1024];
	 uint16_t length = fs.readMemoryBlock(buffer, sizeof(buffer));
	 fin = fs.isFinished();
	 connection->send(buffer, length, ft, fin);
	 fs.seek(length);
	 ft = WS_CONTINUATION_FRAME;
	 } while (!fin);

	 delete fs;
	 */
}

ioerror_t FileManager::startUpload(command_connection_t connection, JsonObject& json)
{
	const char* name = json[ATTR_NAME()];
	size_t size = json[ATTR_SIZE()];
	if (name == nullptr || size <= 0)
		return setError(json, ioe_bad_param);

	m_upload = new FileUpload(*this, connection);
	if (m_upload == nullptr)
		return setError(json, ioe_nomem);

	if (!m_upload->init(name, size)) {
		delete m_upload;
		return setError(json, ioe_file);
	}

	debug_i("File upload '%s', %u bytes", name, size);
	setPending(json);
	return ioe_success;
}

void FileManager::handleMessage(command_connection_t connection, JsonObject& json)
{
	endUpload();

	const char* cmd = json[ATTR_COMMAND()];
	if (COMMAND_GET() == cmd)
		getFile(connection, json);
	else if (COMMAND_UPLOAD() == cmd)
		startUpload(connection, json);
	else if (COMMAND_LIST() == cmd)
		listFiles(json);
	else if (COMMAND_DELETE() == cmd)
		deleteFiles(json);
	else if (COMMAND_INFO() == cmd)
		getInfo(json);
	else if (COMMAND_CHECK() == cmd)
		check(json);
	else if (COMMAND_FORMAT() == cmd)
		format(json);
	else
		ICommandHandler::handleMessage(connection, json);
}

