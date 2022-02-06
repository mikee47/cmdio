/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include "FileManager.h"
#include <Storage/PartitionStream.h>

using namespace FileUtils;

DEFINE_FSTR_LOCAL(ATTR_WRITTEN, "written")

#define UPLOAD_TIMEOUT_MS 2000

/**
 * @brief Hook files of the format "@@nnnnnnnn.ulf"
 * @retval bool true if file matches, false otherwise
 *
 * If filename is valid, content will be written to flash at offset 0xnnnnnnnn.
 * On success, file is deleted.
 * On failure, file is truncated to zero length.
 */
bool FileUpload::initFlashUpload()
{
	if(connection == nullptr || connection->getAccess() < UserRole::Admin) {
		return false;
	}
	if(!fileName.startsWith(F("@@")) || !fileName.endsWith(F(".ulf"))) {
		return false;
	}

	String partName = fileName;
	partName.setLength(partName.length() - 4);
	partName.remove(0, 2);

	auto part = Storage::findPartition(partName);
	if(!part) {
		debug_e("[FUP] Partition '%s' not found", partName.c_str());
		return false;
	}

	// TODO: Block writes to running firmware image

	stream.reset(new Storage::PartitionStream(part, true));
	return true;
}

int FileUpload::init(const char* filename, size_t size)
{
	fileName = filename;
	fileSize = size;

	if(!initFlashUpload()) {
		auto file = new FileStream;
		if(!file->open(fileName, File::CreateNewAlways | File::WriteOnly)) {
			int err = file->getLastError();
			delete file;
			return err;
		}
		stream.reset(file);
	}

	error = ERROR_TIMEOUT;
	timer.initializeMs<UPLOAD_TIMEOUT_MS>([](void* param) { static_cast<FileUpload*>(param)->endUpload(); }, this)
		.startOnce();

	return FS_OK;
}

bool FileUpload::handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
{
	if(this->connection != connection || !stream) {
		return false;
	}

	debug_d("FileUpload::handleData(%u)", size);

	timer.stop();

	if(stream->write(data, size) != size) {
		debug_e("File write error");
		error = IFS::Error::WriteFailure;
	} else {
		bytesWritten += size;
		// Need more data
		if(bytesWritten < fileSize) {
			timer.startOnce();
			return true;
		}

		error = (bytesWritten == fileSize) ? FS_OK : ERROR_TOO_BIG;
	}

	endUpload();
	return true;
}

void FileUpload::endUpload()
{
	stream.reset();
	if(connection) {
		DynamicJsonDocument doc(1024);
		auto json = doc.to<JsonObject>();
		json[ATTR_NAME] = fileName;
		json[ATTR_METHOD] = String(METHOD_FILES);
		json[ATTR_COMMAND] = String(COMMAND_UPLOAD);
		json[ATTR_WRITTEN] = bytesWritten;
		FileUtils::getFileInfo(json, fileName);
		if(error) {
			IO::setError(json, error, fileGetErrorString(error));
		} else {
			IO::setSuccess(json);
		}
		connection->send(json);
	}

	manager.endUpload();
}
