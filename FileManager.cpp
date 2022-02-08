/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include "FileManager.h"
#include <LittleFS.h>
#include <IFS/FileCopier.h>

#ifdef USE_LOCAL_FILESYSTEM
#include <IFS/Host/FileSystem.h>
#endif

using namespace FileUtils;

namespace
{
// config
DEFINE_FSTR_LOCAL(FILE_FILEMGR_CONFIG, ".filemgr.json")
DEFINE_FSTR_LOCAL(ATTR_FWFS, "fwfs")
// LIST
DEFINE_FSTR_LOCAL(ATTR_FILES, "files")
DEFINE_FSTR_LOCAL(ATTR_DIR, "dir")
// DELETE
DEFINE_FSTR_LOCAL(COMMAND_DELETE, "delete")
// INFO
DEFINE_FSTR_LOCAL(ATTR_VOLUME_SIZE, "volumesize")
DEFINE_FSTR_LOCAL(ATTR_FREE_SPACE, "freespace")
// CHECK
DEFINE_FSTR_LOCAL(COMMAND_CHECK, "check")
// FORMAT
DEFINE_FSTR_LOCAL(COMMAND_FORMAT, "format")
// RESTORE
DEFINE_FSTR_LOCAL(COMMAND_RESTORE, "restore")

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

void format(JsonObject json)
{
	String path = json[ATTR_NAME];
	FileStat stat{};
	fileStats(path, stat);
	if(!stat.attr[FileAttribute::MountPoint]) {
		return (void)IO::setError(json, IO::Error::access_denied);
	}

	// Get filesystem object
	int dir = fileOpen(path, File::ReadOnly);
	if(dir < 0) {
		return (void)IO::setError(json, dir, fileGetErrorString(dir));
	}
	int err = fileStats(dir, stat);
	fileClose(dir);

	if(err == FS_OK) {
		// Format the filesystem
		err = stat.fs->format();
	}

	if(err < 0) {
		return (void)IO::setError(json, IO::Error::file, fileGetErrorString(err));
	}

	IO::setSuccess(json);
}

void restore(JsonObject json)
{
	String filename = json[ATTR_NAME];
	auto fs = fileMountArchive(filename);
	if(!fs) {
		IO::setError(json, IO::Error::file);
		return;
	}

	bool hasErrors = false;

	IFS::FileCopier copier(*fs, *getFileSystem());
	auto errorHandler = [&](IFS::FileSystem& fileSys, int errorCode, IFS::FileCopier::Operation operation,
							const String& path) -> bool {
		auto obj = json["files"].createNestedObject(path);
		obj["operation"] = toString(operation);
		obj["error"] = fileSys.getErrorString(errorCode);
		hasErrors = true;
		return true;
	};
	copier.onError(errorHandler);
	int i = filename.lastIndexOf('/');
	if(i < 0) {
		filename = nullptr;
	} else {
		filename.setLength(i);
	}

	copier.copyDir(nullptr, filename.c_str());
	if(hasErrors) {
		IO::setError(json, IO::Error::file);
	} else {
		IO::setSuccess(json);
	}
}

#ifndef USE_LOCAL_FILESYSTEM
IFS::FileSystem* mountConfig()
{
	auto part = Storage::findPartition(F("config"));
	if(!part) {
		debug_e("Missing config partition");
		return nullptr;
	}
	auto lfs = IFS::createLfsFilesystem(part);
	if(!lfs) {
		return nullptr;
	}
	if(lfs->mount() != FS_OK) {
		delete lfs;
		return nullptr;
	}

	return lfs;
}
#endif

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

	auto configFileSys = mountConfig();
	if(configFileSys != nullptr) {
		// Get current FWFS partition ID from config
		DynamicJsonDocument doc(1024);
		IFS::FileStream fs(configFileSys);
		if(fs.open(FILE_FILEMGR_CONFIG)) {
			Json::deserialize(doc, fs);
			auto json = doc.as<JsonObject>();
			firmwarePartitionNumber = json[ATTR_FWFS];
		}
	}

	String partName = F("fwfs") + firmwarePartitionNumber;
	auto part = Storage::findPartition(partName);
	if(!fwfs_mount(part)) {
		delete configFileSys;
		return false;
	}

	getFileSystem()->setVolume(1, configFileSys);
	return true;
#endif
}

void FileManager::endUpload()
{
	if(upload) {
		if(callback) {
			callback(*upload);
		}
		upload.reset();
	}
}

void FileManager::startUpload(WSCommandConnection* connection, JsonObject json)
{
	upload.reset(new FileUpload(*this, connection));
	if(!upload) {
		IO::setError(json, IO::Error::no_mem);
	} else if(!upload->init(json)) {
		upload.reset();
	}
}

void FileManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	endUpload();

	const char* cmd = json[ATTR_COMMAND];
	if(COMMAND_UPLOAD == cmd) {
		startUpload(connection, json);
	} else if(COMMAND_DELETE == cmd) {
		deleteFiles(json);
	} else if(COMMAND_INFO == cmd) {
		getInfo(json);
	} else if(COMMAND_CHECK == cmd) {
		check(json);
	} else if(COMMAND_FORMAT == cmd) {
		format(json);
	} else if(COMMAND_RESTORE == cmd) {
		restore(json);
	} else {
		WSCommandHandler::handleMessage(connection, json);
	}
}
