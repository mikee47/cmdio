/*
 * FileManager.h
 *
 *  Created on: 28 May 2018
 *      Author: mikee47
 */

#pragma once

#include "CommandHandler.h"
#include "FileUpload.h"
#include <SimpleTimer.h>
#include <Delegate.h>

/**
 * @brief  Callback function for file upload completion
 */
typedef Delegate<void(const FileUpload& upload)> FileUploadDelegate;

class FileManager : public WSCommandHandler
{
public:
	~FileManager()
	{
		endUpload();
	}

	bool init();

	/* WSCommandHandler */
	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::Manager;
	}

	PageInfo getPageInfo() const override
	{
		return {UserRole::Admin, F("Files")};
	}

	void onUpload(FileUploadDelegate callback)
	{
		this->callback = callback;
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size) override
	{
		return upload ? upload->handleData(connection, data, size) : false;
	}

	bool isPartitionMounted(Storage::Partition part)
	{
		return part.name() == (F("fwfs") + firmwarePartitionNumber);
	}

	uint8_t getFirmwarePartitionNumber() const
	{
		return firmwarePartitionNumber;
	}

private:
	friend FileUpload;

	IO::ErrorCode getFile(WSCommandConnection* connection, JsonObject json);
	void startUpload(WSCommandConnection* connection, JsonObject json);
	void endUpload();

private:
	std::unique_ptr<FileUpload> upload;
	FileUploadDelegate callback = nullptr;
	uint8_t firmwarePartitionNumber{0};
};
