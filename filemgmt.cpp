/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#include "filemgmt.h"

#include <status.h>
#include "Services/IFS/HybridFileSystem.h"
#include "Services/IFS/IFSFlashMedia.h"

#include "../Services/SpifFS/spiffs_sming.h"

static DEFINE_STRING_P(METHOD_FILES, "files")

// LIST
static DEFINE_STRING_P(COMMAND_LIST, "list")
static DEFINE_STRING_P(ATTR_FILES, "files")
// GET
static DEFINE_STRING_P(COMMAND_GET, "get")
// UPLOAD
static DEFINE_STRING_P(COMMAND_UPLOAD, "upload")
// DELETE
static DEFINE_STRING_P(COMMAND_DELETE, "delete")
// STAT/UPLOAD
static DEFINE_STRING_P(ATTR_SIZE, "size")
static DEFINE_STRING_P(ATTR_FLAGS, "flags")
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
	// Needs the cast to make Json create a copy of the string
	json[ATTR_NAME()] = String(stat.name);
	json[ATTR_SIZE()] = stat.size;
	json[ATTR_ACCESS()] = stat.acl.toString();
	json[ATTR_FLAGS()] = stat.attrStr();
	json[ATTR_MTIME()] = stat.mtime;
}

static void getFileInfo(JsonObject& json, file_t file)
{
	FileStat stat;
	if (fileStats(file, &stat) >= 0)
		getFileInfo(json, stat);
}

bool CFileUpload::init(const char* filename, size_t size)
{
	m_filename = filename;
	m_size = size;
	m_file = fileOpen(m_filename, eFO_CreateNewAlways | eFO_WriteOnly);
	debug_i("fileOpen('%s'): %d", filename, m_file);
	if (m_file < 0)
		return false;

	m_error = ERROR_TIMEOUT;
	m_timer.setCallback([](void* arg) {
		reinterpret_cast<CFileUpload*>(arg)->endUpload();
	}, this);
	m_timer.startMs(UPLOAD_TIMEOUT_MS);

	return true;
}

void CFileUpload::close()
{
	if (m_file >= 0) {
		fileClose(m_file);
		m_file = -1;
	}
}

bool CFileUpload::handleData(command_connection_t connection, uint8_t* data, size_t size)
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

void CFileUpload::endUpload()
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

CFileManager::~CFileManager()
{
	endUpload();
}


uint32_t getFlashAddress(const void* addr)
{
	return reinterpret_cast<uint32_t>(addr) - INTERNAL_FLASH_START_ADDRESS;
}


bool CFileManager::init()
{
	fileFreeFileSystem();

	auto freeheap = system_get_free_heap_size();
	debug_i("1: free heap = %u", freeheap);

	auto cfg = spiffs_get_storage_config();
//	auto fs = new HybridFileSystem(getFlashAddress(__fwfiles_data), cfg.phys_addr, cfg.phys_size);
	auto fs = new FirmwareFileSystem(getFlashAddress(__fwfiles_data));
	debug_i("2: heap used = %u", freeheap - system_get_free_heap_size());
	if (!fs)
		return false;

	int res = fs->mount();

	debug_i("3: Heap used = %u", freeheap - system_get_free_heap_size());

	if (res < 0) {
		delete fs;
		return false;
	}

	fileSetFileSystem(fs);
	return true;
}

void CFileManager::endUpload()
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

static void listFiles(JsonObject& json)
{
	auto& files = json.createNestedArray(ATTR_FILES());

	filedir_t dir;
	if (fileOpenRootDir(&dir) >= 0) {
		FileStat stat;
		while (fileReadDir(dir, &stat) >= 0) {
			auto& file = files.createNestedObject();
			getFileInfo(file, stat);
		}
		fileCloseDir(dir);
	}

	setSuccess(json);
}

static void deleteFiles(JsonObject& json)
{
	JsonArray& files = json[ATTR_FILES()];
	for (unsigned i = 0; i < files.size(); ++i) {
		JsonObject& file = files[i];
		int res = fileDelete(file[ATTR_NAME()].asString());
		if (res < 0)
			setError(file, res, fileGetErrorString(res));
		else
			setSuccess(file);
	}
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
//  setError(json, ioe_not_impl);

// @todo Causes alignment exception. Not investigated.

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

String CFileManager::getMethod() const
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
ioerror_t CFileManager::getFile(command_connection_t connection, JsonObject& json)
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

ioerror_t CFileManager::startUpload(command_connection_t connection, JsonObject& json)
{
	const char* name = json[ATTR_NAME()];
	size_t size = json[ATTR_SIZE()];
	if (name == nullptr || size <= 0)
		return setError(json, ioe_bad_param);

	m_upload = new CFileUpload(*this, connection);
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

void CFileManager::handleMessage(command_connection_t connection, JsonObject& json)
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
		CCommandHandler::handleMessage(connection, json);
}

