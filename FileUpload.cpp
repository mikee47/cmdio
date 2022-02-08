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
 * @retval IO::ErrorCode IO::Error::not_impl if filename doesn't match, otherwise result code
 *
 * If filename is valid, content will be written to flash at offset 0xnnnnnnnn.
 * On success, file is deleted.
 * On failure, file is truncated to zero length.
 */
IO::ErrorCode FileUpload::initFlashUpload(JsonObject json)
{
	if(!fileName.startsWith(F("@@")) || !fileName.endsWith(F(".ulf"))) {
		return IO::Error::not_impl;
	}
	if(connection == nullptr || connection->getAccess() < UserRole::Admin) {
		return IO::setError(json, IO::Error::access_denied);
	}

	String partName = fileName;
	partName.setLength(partName.length() - 4);
	partName.remove(0, 2);

	auto part = Storage::findPartition(partName);
	if(!part) {
		return IO::setError(json, IO::Error::bad_param, partName);
	}
	if(part.type() == Storage::Partition::Type::app) {
		return IO::setError(json, IO::Error::access_denied);
	}
	if(manager.isPartitionMounted(part)) {
		return IO::setError(json, IO::Error::access_denied);
	}

	stream.reset(new Storage::PartitionStream(part, true));
	return IO::Error::success;
}

bool FileUpload::init(JsonObject json)
{
	fileName = json[ATTR_NAME].as<const char*>();
	fileSize = json[ATTR_SIZE];
	if(fileName.length() == 0 || fileSize == 0) {
		IO::setError(json, IO::Error::bad_param);
		return false;
	}

	auto err = initFlashUpload(json);
	if(err == IO::Error::not_impl) {
		auto file = new FileStream;
		if(!file->open(fileName, File::CreateNewAlways | File::WriteOnly)) {
			IO::setError(json, IO::Error::file, file->getLastErrorString());
			delete file;
			return false;
		}
		stream.reset(file);
	} else if(err != IO::Error::success) {
		return false;
	}

	error = ERROR_TIMEOUT;
	timer.initializeMs<UPLOAD_TIMEOUT_MS>([](void* param) { static_cast<FileUpload*>(param)->endUpload(); }, this)
		.startOnce();

	debug_i("File upload '%s', %u bytes", fileName.c_str(), fileSize);
	IO::setPending(json);
	return true;
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
