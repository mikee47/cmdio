/*
 * FileManager.h
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#ifndef __FILE_MANAGER_H
#define __FILE_MANAGER_H

#include "CommandHandler.h"
#include "status.h"
#include "FileSystem.h"
#include "SimpleTimer.h"
#include "Delegate.h"
#include <functional>

class FileManager;

/** @brief File upload errors
 *  @note mapped to user-defined filing system error range
 */
#define ERROR_TIMEOUT (FSERR_USER - 1) ///< Transfer timed out
#define ERROR_TOO_BIG (FSERR_USER - 2) ///< Received more file data than header indicated

DECLARE_FSTR(ATTR_ACCESS)

// For handling a file upload
class FileUpload
{
public:
	FileUpload(FileManager& manager, WSCommandConnection* connection) : manager(manager), connection(connection)
	{
	}

	~FileUpload()
	{
		close();
	}

	bool init(const char* filename, size_t size);

	const String& filename() const
	{
		return fileName;
	}

	int getError() const
	{
		return error;
	}

	bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size);

private:
	void close();
	void endUpload();

private:
	FileManager& manager;
	String fileName = nullptr;
	file_t fileHandle = -1;
	uint32_t fileSize = 0;
	WSCommandConnection* connection = nullptr;
	uint32_t bytesWritten = 0;
	// SPIFFS error
	int error = ERROR_TIMEOUT;
	// Handles timeout condition
	SimpleTimer timer;
};

/** @brief  Callback function for file upload completion
 *
 * @param filename The file which has changed
 * @param error The SPIFFS error code
 */
typedef std::function<void(const FileUpload& upload)> file_upload_callback_t;

class FileManager : public WSCommandHandler
{
	friend FileUpload;

public:
	~FileManager();

	bool init(const void* fwfsImageData);

	/* CCommandHandler */
	String getMethod() const;

	UserRole minAccess() const
	{
		return UserRole::User;
	}

	void onUpload(file_upload_callback_t callback)
	{
		this->callback = callback;
	}

	void handleMessage(WSCommandConnection* connection, JsonObject& json);

	bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
	{
		return upload ? upload->handleData(connection, data, size) : false;
	}

private:
	ioerror_t getFile(WSCommandConnection* connection, JsonObject& json);
	ioerror_t startUpload(WSCommandConnection* connection, JsonObject& json);
	void endUpload();

private:
	FileUpload* upload;
	file_upload_callback_t callback;
};

#endif // __FILE_MANAGER_H
