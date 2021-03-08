/*
 * FileManager.h
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#pragma once

#include "CommandConnection.h"
#include "FileUtils.h"
#include <SimpleTimer.h>

class FileManager;

/**
 * @brief File upload errors
 * @note mapped to user-defined filing system error range
 */
constexpr IFS::ErrorCode ERROR_TIMEOUT = IFS::Error::USER - 1; ///< Transfer timed out
constexpr IFS::ErrorCode ERROR_TOO_BIG = IFS::Error::USER - 2; ///< Received more file data than header indicated

/**
 * @brief Used by FileManager to handle uploads
 */
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

	int init(const char* filename, size_t size);

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
	String fileName;
	file_t fileHandle{-1};
	uint32_t fileSize{0};
	WSCommandConnection* connection{nullptr};
	uint32_t bytesWritten{0};
	// SPIFFS error
	int error{ERROR_TIMEOUT};
	// Handles timeout condition
	SimpleTimer timer;
};
