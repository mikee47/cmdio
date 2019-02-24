/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include <status.h>
#include "FileManager.h"

// Enable this to use hybrid filesystem. Defaults to FWFS (firmware filesystem, read-only)
#define FWFS_HYBRID

DEFINE_FSTR(ATTR_ACCESS, "access")

static DEFINE_FSTR(METHOD_FILES, "files");

// LIST
static DEFINE_FSTR(COMMAND_LIST, "list");
static DEFINE_FSTR(ATTR_FILES, "files");
static DEFINE_FSTR(ATTR_DIR, "dir");
// GET
static DEFINE_FSTR(COMMAND_GET, "get");
// UPLOAD
static DEFINE_FSTR(COMMAND_UPLOAD, "upload");
// DELETE
static DEFINE_FSTR(COMMAND_DELETE, "delete");
// STAT/UPLOAD
static DEFINE_FSTR(ATTR_SIZE, "size");
static DEFINE_FSTR(ATTR_ATTR, "attr");
static DEFINE_FSTR(ATTR_MTIME, "mtime");
static DEFINE_FSTR(ATTR_WRITTEN, "written");
// INFO
static DEFINE_FSTR(ATTR_VOLUME_SIZE, "volumesize");
static DEFINE_FSTR(ATTR_FREE_SPACE, "freespace");
// CHECK
static DEFINE_FSTR(COMMAND_CHECK, "check");
// FORMAT
static DEFINE_FSTR(COMMAND_FORMAT, "format");

#define UPLOAD_TIMEOUT_MS 2000

static bool IsValidUtf8(const char* str, unsigned length)
{
	if(str == nullptr) {
		return true;
	}

	unsigned i = 0;
	while(i < length) {
		char c = str[i++];
		if((c & 0x80) == 0) {
			continue;
		}

		if(i >= length) {
			return false; // incomplete multibyte char
		}

		if(c & 0x20) {
			c = str[i++];
			if((c & 0xC0) != 0x80) {
				return false; // malformed trail byte or out of range char
			}
			if(i >= length) {
				return false; // incomplete multibyte char
			}
		}

		c = str[i++];
		if((c & 0xC0) != 0x80) {
			return false; // malformed trail byte
		}
	}

	return true;
}

// Check for invalid characters and replace them - can break browser operation otherwise
static char* checkString(char* str, unsigned length)
{
	if(!IsValidUtf8(str, length)) {
		debug_w("Invalid UTF8: %s", str);
		for(unsigned i = 0; i < length; ++i) {
			char& c = str[i];
			if(c < 0x20 || c > 127)
				c = '_';
		}
	}
	return str;
}

static void getFileInfo(JsonObject& json, const FileStat& stat)
{
	String attrName = ATTR_NAME;
	if(!json.containsKey(attrName)) {
		json[attrName] = stat.name.length ? String(checkString(stat.name.buffer, stat.name.length)) : String::empty;
	}
	json[ATTR_SIZE] = stat.size;
	FileSystemInfo fsi;
	stat.fs->getinfo(fsi);
	json["fs"] = (int)fsi.type;
	char buf[10];
	json[ATTR_ACCESS] = String(fileAclToStr(stat.acl, buf, sizeof(buf)));
	json[ATTR_ATTR] = String(fileAttrToStr(stat.attr, buf, sizeof(buf)));
	json[ATTR_MTIME] = stat.mtime;
}

static void getFileInfo(JsonObject& json, file_t file)
{
	FileNameStat stat;
	if(fileStats(file, &stat) >= 0) {
		getFileInfo(json, stat);
	}
}

bool FileUpload::init(const char* filename, size_t size)
{
	fileName = filename;
	fileSize = size;
	fileHandle = fileOpen(fileName, eFO_CreateNewAlways | eFO_WriteOnly);
	debug_i("fileOpen('%s'): %d", filename, fileHandle);
	if(fileHandle < 0) {
		return false;
	}

	error = ERROR_TIMEOUT;
	timer.setCallback([](void* arg) { reinterpret_cast<FileUpload*>(arg)->endUpload(); }, this);
	timer.startMs(UPLOAD_TIMEOUT_MS);

	return true;
}

void FileUpload::close()
{
	if(fileHandle >= 0) {
		fileClose(fileHandle);
		fileHandle = -1;
	}
}

bool FileUpload::handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
{
	if(this->connection != connection) {
		return false;
	}

	debug_i("FileUpload::handleData(%u)", size);

	timer.stop();

	int n = fileWrite(fileHandle, data, size);
	if(n != (int)size) {
		debug_e("File write error");
		error = n;
	} else {
		bytesWritten += size;
		// Need more data
		if(bytesWritten < fileSize) {
			timer.startMs(UPLOAD_TIMEOUT_MS);
			return true;
		}

		error = (bytesWritten == fileSize) ? FS_OK : ERROR_TOO_BIG;
	}

	endUpload();
	return true;
}

void FileUpload::endUpload()
{
	if(fileHandle >= 0) {
		fileFlush(fileHandle);
	}

	if(connection) {
		DynamicJsonBuffer buffer;
		JsonObject& json = buffer.createObject();
		json[ATTR_METHOD] = String(METHOD_FILES);
		json[ATTR_COMMAND] = String(COMMAND_UPLOAD);
		json[ATTR_WRITTEN] = bytesWritten;
		getFileInfo(json, fileHandle);
		if(error) {
			setError(json, error, fileGetErrorString(error));
		} else {
			setSuccess(json);
		}
		connection->send(json);
	}
	close();

	manager.endUpload();
}

/* CFileManager */

FileManager::~FileManager()
{
	endUpload();
}

bool FileManager::init(const void* fwfsImageData)
{
	fileFreeFileSystem();

	auto freeheap = system_get_free_heap_size();
	debug_i("1: heap = %u", freeheap);

	IFileSystem* fs;
#ifdef FWFS_HYBRID
	fs = CreateHybridFilesystem(fwfsImageData);
#else
	fs = CreateFirmwareFilesystem(fwfsImageData);
#endif
	debug_i("2: heap = -%u", freeheap - system_get_free_heap_size());

	if(fs == nullptr) {
		debug_e("Failed to created filesystem object");
		return false;
	}

	int res = fs->mount();
	debug_i("3: heap = -%u", freeheap - system_get_free_heap_size());

	char buf[20];
	fs->geterrortext(res, buf, sizeof(buf));
	debug_i("mount() returned %d (%s)", res, buf);

	if(res < 0) {
		delete fs;
		return false;
	}

	fileSetFileSystem(fs);
	return true;
}

void FileManager::endUpload()
{
	if(upload) {
		if(callback) {
			callback(*upload);
		}
		delete upload;
		upload = nullptr;
	}
}

static JsonObject& findOrCreateFile(JsonArray& files, const String& name)
{
	String attrName = ATTR_NAME;

	for(auto& f : files) {
		if(name == f[attrName]) {
			return f;
		}
	}

	auto& f = files.createNestedObject();
	String s = name;
	checkString(s.begin(), s.length());
	f[attrName] = s;
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
	auto& files = json.createNestedArray(ATTR_FILES);

	filedir_t dir;
	int res = fileOpenDir(json[ATTR_DIR].asString(), &dir);
	if(res >= 0) {
		FileNameStat stat;
		while((res = fileReadDir(dir, &stat)) >= 0) {
			JsonObject& file = findOrCreateFile(files, stat.name.buffer);
			getFileInfo(file, stat);
		}
		fileCloseDir(dir);
	}

	if(res == FS_OK || res == FSERR_NoMoreFiles) {
		setSuccess(json);
	} else {
		setError(json, res, fileGetErrorString(res));
	}
}

static void deleteFiles(JsonObject& json)
{
	int res = FS_OK;
	String dir = json[ATTR_DIR].asString();
	if(dir)
		dir += '/';
	JsonArray& files = json[ATTR_FILES];
	String attrName = ATTR_NAME;
	for(unsigned i = 0; i < files.size(); ++i) {
		JsonObject& file = files[i];
		String path = dir + file[attrName].asString();
		int err = fileDelete(path);
		if(err < 0) {
			setError(file, err, fileGetErrorString(err));
			if(res == FS_OK)
				res = err;
		} else {
			setSuccess(file);
		}
	}

	if(res == FS_OK) {
		setSuccess(json);
	} else {
		setError(json, res, fileGetErrorString(res));
	}
}

static void getInfo(JsonObject& json)
{
	FileSystemInfo info;
	int err = fileGetSystemInfo(info);
	if(err) {
		setError(json, err, fileGetErrorString(err));
		return;
	}
	json[ATTR_VOLUME_SIZE] = info.volumeSize;
	json[ATTR_FREE_SPACE] = info.freeSpace;
	setSuccess(json);
}

static void check(JsonObject& json)
{
	int err = fileSystemCheck();
	if(err) {
		setError(json, err, fileGetErrorString(err));
	} else {
		setSuccess(json);
	}
}

/*
 * Reformat SPIFFS to blank state
 */
static void format(JsonObject& json)
{
	int err = fileSystemFormat();
	if(err < 0) {
		setError(json, err, fileGetErrorString(err));
	} else {
		setSuccess(json);
	}
}

String FileManager::getMethod() const
{
	return METHOD_FILES;
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
ioerror_t FileManager::getFile(WSCommandConnection* connection, JsonObject& json)
{
	return setError(json, ioe_not_impl);
	/*

	 const char* path = json[ATTR_PATH()];
	 if (!path) {
		 setError(json, ioe_bad_param);
		 return;
	 }

	 file_t fh = fileOpen(path, eFO_ReadOnly);
	 if (fh < 0)
	 	 return ioe_spiffs;

	 filestream_t fs = openFile(fh);
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

ioerror_t FileManager::startUpload(WSCommandConnection* connection, JsonObject& json)
{
	const char* name = json[ATTR_NAME];
	size_t size = json[ATTR_SIZE];
	if(name == nullptr || size <= 0) {
		return setError(json, ioe_bad_param);
	}

	upload = new FileUpload(*this, connection);
	if(upload == nullptr) {
		return setError(json, ioe_nomem);
	}

	if(!upload->init(name, size)) {
		delete upload;
		return setError(json, ioe_file);
	}

	debug_i("File upload '%s', %u bytes", name, size);
	setPending(json);
	return ioe_success;
}

void FileManager::handleMessage(WSCommandConnection* connection, JsonObject& json)
{
	endUpload();

	const char* cmd = json[ATTR_COMMAND];
	if(COMMAND_GET == cmd)
		getFile(connection, json);
	else if(COMMAND_UPLOAD == cmd)
		startUpload(connection, json);
	else if(COMMAND_LIST == cmd)
		listFiles(json);
	else if(COMMAND_DELETE == cmd)
		deleteFiles(json);
	else if(COMMAND_INFO == cmd)
		getInfo(json);
	else if(COMMAND_CHECK == cmd)
		check(json);
	else if(COMMAND_FORMAT == cmd)
		format(json);
	else
		WSCommandHandler::handleMessage(connection, json);
}
