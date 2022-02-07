/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#include "FileManager.h"

using namespace FileUtils;

DEFINE_FSTR_LOCAL(ATTR_WRITTEN, "written")

#define UPLOAD_TIMEOUT_MS 2000

int FileUpload::init(const char* filename, size_t size)
{
	fileName = filename;
	fileSize = size;
	fileHandle = fileOpen(fileName, File::CreateNewAlways | File::WriteOnly);
	debug_i("fileOpen('%s'): %d", filename, fileHandle);
	if(fileHandle < 0) {
		return fileHandle;
	}

	error = ERROR_TIMEOUT;
	timer.initializeMs<UPLOAD_TIMEOUT_MS>([](void* param) { static_cast<FileUpload*>(param)->endUpload(); }, this)
		.startOnce();

	return FS_OK;
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
	if(fileHandle >= 0) {
		fileFlush(fileHandle);
	}

	if(connection) {
		DynamicJsonDocument doc(1024);
		auto json = doc.to<JsonObject>();
		json[ATTR_NAME] = fileName;
		json[ATTR_METHOD] = String(METHOD_FILES);
		json[ATTR_COMMAND] = String(COMMAND_UPLOAD);
		json[ATTR_WRITTEN] = bytesWritten;
		FileUtils::getFileInfo(json, fileHandle);
		if(error) {
			IO::setError(json, error, fileGetErrorString(error));
		} else {
			IO::setSuccess(json);
		}
		connection->send(json);
	}
	close();

	manager.endUpload();
}
