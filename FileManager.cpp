/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include "FileManager.h"
#include <LittleFS.h>
#ifdef ARCH_HOST
#include <IFS/Host/FileSystem.h>
#endif

using namespace FileUtils;

namespace
{
// LIST
DEFINE_FSTR_LOCAL(COMMAND_LIST, "list")
DEFINE_FSTR_LOCAL(ATTR_FILES, "files")
DEFINE_FSTR_LOCAL(ATTR_DIR, "dir")
// GET
DEFINE_FSTR_LOCAL(COMMAND_GET, "get")
// DELETE
DEFINE_FSTR_LOCAL(COMMAND_DELETE, "delete")
// INFO
DEFINE_FSTR_LOCAL(ATTR_VOLUME_SIZE, "volumesize")
DEFINE_FSTR_LOCAL(ATTR_FREE_SPACE, "freespace")
// CHECK
DEFINE_FSTR_LOCAL(COMMAND_CHECK, "check")
// FORMAT
DEFINE_FSTR_LOCAL(COMMAND_FORMAT, "format")

JsonObject findOrCreateFile(JsonArray& files, const String& name)
{
	String attrName = ATTR_NAME;

	for(auto f : files) {
		if(name == f[attrName]) {
			return f;
		}
	}

	JsonObject f = files.createNestedObject();
	String s = name;
	FileUtils::checkString(s.begin(), s.length());
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
void listFiles(JsonObject json)
{
	JsonArray files = json.createNestedArray(ATTR_FILES);

	DirHandle dir;
	int res = fileOpenDir(json[ATTR_DIR].as<const char*>(), dir);
	if(res >= 0) {
		FileNameStat stat;
		while((res = fileReadDir(dir, stat)) >= 0) {
			JsonObject file = findOrCreateFile(files, stat.name.buffer);
			FileUtils::getFileInfo(file, stat);
		}
		fileCloseDir(dir);
	}

	if(res == FS_OK || res == IFS::Error::NoMoreFiles) {
		IO::setSuccess(json);
	} else {
		IO::setError(json, res, fileGetErrorString(res));
	}
}

void deleteFiles(JsonObject json)
{
	int res = FS_OK;
	String dir;
	if(Json::getValue(json[ATTR_DIR], dir)) {
		dir += '/';
	}
	JsonArray files = json[ATTR_FILES];
	String attrName = ATTR_NAME;
	for(auto file : files) {
		String path = dir + static_cast<const char*>(file[attrName]);
		int err = fileDelete(path);
		if(err < 0) {
			IO::setError(file, err, fileGetErrorString(err));
			if(res == FS_OK)
				res = err;
		} else {
			IO::setSuccess(file);
		}
	}

	if(res == FS_OK) {
		IO::setSuccess(json);
	} else {
		IO::setError(json, res, fileGetErrorString(res));
	}
}

void getInfo(JsonObject json)
{
	IFS::IFileSystem::Info info;
	int err = fileGetSystemInfo(info);
	if(err) {
		IO::setError(json, err, fileGetErrorString(err));
		return;
	}
	json[ATTR_VOLUME_SIZE] = info.volumeSize;
	json[ATTR_FREE_SPACE] = info.freeSpace;
	IO::setSuccess(json);
}

void check(JsonObject json)
{
	int err = fileSystemCheck();
	if(err) {
		IO::setError(json, err, fileGetErrorString(err));
	} else {
		IO::setSuccess(json);
	}
}

/*
 * Reformat SPIFFS to blank state
 */
void format(JsonObject json)
{
	// Open a handle to the root LFS partition
	int dir = fileOpen("config", File::ReadOnly);
	if(dir < 0) {
		IO::setError(json, dir, fileGetErrorString(dir));
		return;
	}
	// Get filesystem object
	FileStat stat;
	int err = fileStats(dir, stat);
	fileClose(dir);
	if(err == FS_OK) {
		// Format the filesystem
		err = stat.fs->format();
	}
	if(err < 0) {
		IO::setError(json, err, fileGetErrorString(err));
		return;
	}

	IO::setSuccess(json);
}

} // namespace

String FileManager::getMethod() const
{
	return METHOD_FILES;
}

bool FileManager::init()
{
	fileFreeFileSystem();

#ifdef USE_LOCAL_FILESYSTEM

	int err = symlink(PROJECT_DIR "/config", PROJECT_DIR "/files/config");
	(void)err;
	atexit([]() { remove(PROJECT_DIR "/files/config"); });
	auto fs = new IFS::Host::FileSystem(PROJECT_DIR "/files");
	return fileMountFileSystem(fs);

#else

#if DEBUG_VERBOSE_LEVEL >= INFO
	auto freeheap = system_get_free_heap_size();
#endif
	debug_i("1: heap = %u", freeheap);
	if(!fwfs_mount()) {
		return false;
	}
	debug_i("2: heap = -%u", freeheap - system_get_free_heap_size());

	auto part = Storage::findPartition(F("config"));
	if(!part) {
		debug_e("Missing config partition");
		return false;
	}
	auto lfs = IFS::createLfsFilesystem(part);
	if(!lfs || lfs->mount() != FS_OK) {
		return false;
	}
	assert(getFileSystem()->setVolume(1, lfs) == FS_OK);

	return true;
#endif
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
IO::ErrorCode FileManager::getFile(WSCommandConnection* connection, JsonObject json)
{
	return IO::setError(json, IO::Error::not_impl);
	/*

	 const char* path = json[ATTR_PATH()];
	 if (!path) {
		 IO::setError(json, IO::Error::bad_param);
		 return;
	 }

	 file_t fh = fileOpen(path, eFO_ReadOnly);
	 if (fh < 0)
	 	 return IO::Error::spiffs;

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

IO::ErrorCode FileManager::startUpload(WSCommandConnection* connection, JsonObject json)
{
	const char* name = json[ATTR_NAME];
	size_t size = json[ATTR_SIZE];
	if(name == nullptr || size <= 0) {
		return IO::setError(json, IO::Error::bad_param);
	}

	upload = new FileUpload(*this, connection);
	if(upload == nullptr) {
		return IO::setError(json, IO::Error::no_mem);
	}

	int error = upload->init(name, size);
	if(error < 0) {
		delete upload;
		upload = nullptr;
		IO::setError(json, error, fileGetErrorString(error));
		return IO::Error::file;
	}

	debug_i("File upload '%s', %u bytes", name, size);
	IO::setPending(json);
	return IO::Error::success;
}

void FileManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	endUpload();

	const char* cmd = json[ATTR_COMMAND];
	if(COMMAND_GET == cmd) {
		getFile(connection, json);
	} else if(COMMAND_UPLOAD == cmd) {
		startUpload(connection, json);
	} else if(COMMAND_LIST == cmd) {
		listFiles(json);
	} else if(COMMAND_DELETE == cmd) {
		deleteFiles(json);
	} else if(COMMAND_INFO == cmd) {
		getInfo(json);
	} else if(COMMAND_CHECK == cmd) {
		check(json);
	} else if(COMMAND_FORMAT == cmd) {
		format(json);
	} else {
		WSCommandHandler::handleMessage(connection, json);
	}
}
