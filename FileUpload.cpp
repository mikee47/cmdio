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
	if(!file.open(fileName, File::CreateNewAlways | File::WriteOnly)) {
		return file.getLastError();
	}

	error = ERROR_TIMEOUT;
	timer.initializeMs<UPLOAD_TIMEOUT_MS>([](void* param) { static_cast<FileUpload*>(param)->endUpload(); }, this)
		.startOnce();

	return FS_OK;
}

bool FileUpload::handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
{
	if(this->connection != connection) {
		return false;
	}

	debug_i("FileUpload::handleData(%u)", size);

	timer.stop();

	int n = file.write(data, size);
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
	if(file) {
		file.flush();
	}

	if(connection) {
		DynamicJsonDocument doc(1024);
		auto json = doc.to<JsonObject>();
		json[ATTR_NAME] = fileName;
		json[ATTR_METHOD] = String(METHOD_FILES);
		json[ATTR_COMMAND] = String(COMMAND_UPLOAD);
		json[ATTR_WRITTEN] = bytesWritten;
		FileUtils::getFileInfo(json, file);
		if(error) {
			IO::setError(json, error, fileGetErrorString(error));
		} else {
			IO::setSuccess(json);
		}
		connection->send(json);
	}
	file.close();

	manager.endUpload();
}
