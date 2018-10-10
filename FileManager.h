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
#define ERROR_TIMEOUT   (FSERR_USER - 1)	///< Transfer timed out
#define ERROR_TOO_BIG	(FSERR_USER - 2)	///< Received more file data than header indicated

DECLARE_FSTR(ATTR_ACCESS)

// For handling a file upload
class FileUpload
{
public:

	FileUpload(FileManager& manager, command_connection_t connection) :
		m_manager(manager),
			m_connection(connection)
	{
	}

	~FileUpload()
	{
		close();
	}

	bool init(const char* filename, size_t size);

	const String& filename() const
	{
		return m_filename;
	}

	int error() const
	{
		return m_error;
	}

	bool handleData(command_connection_t connection, uint8_t* data, size_t size);

private:

	void close();
	void endUpload();

private:
	FileManager& m_manager;
	String m_filename = nullptr;
	file_t m_file = -1;
	uint32_t m_size = 0;
	command_connection_t m_connection = nullptr;
	uint32_t m_written = 0;
	// SPIFFS error
	int m_error = ERROR_TIMEOUT;
	// Handles timeout condition
	SimpleTimer m_timer;

};

/** @brief  Callback function for file upload completion
 *
 * @param filename The file which has changed
 * @param error The SPIFFS error code
 */
typedef std::function<void(const FileUpload& upload)> file_upload_callback_t;

class FileManager: public ICommandHandler
{
	friend FileUpload;

public:
	~FileManager();

	bool init();

	/* CCommandHandler */
	String getMethod() const;

	UserRole minAccess() const
	{
		return UserRole::Admin;
	}

	void onUpload(file_upload_callback_t callback)
	{
		m_callback = callback;
	}

	void handleMessage(command_connection_t connection, JsonObject& json);

	bool handleData(command_connection_t connection, uint8_t* data, size_t size)
	{
		return m_upload ? m_upload->handleData(connection, data, size) : false;
	}

private:
	ioerror_t getFile(command_connection_t connection, JsonObject& json);
	ioerror_t startUpload(command_connection_t connection, JsonObject& json);
	void endUpload();

private:
	FileUpload* m_upload;
	file_upload_callback_t m_callback;

};

#endif // __FILE_MANAGER_H
