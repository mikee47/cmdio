/*
 * files.h
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#ifndef __FILEMGMT_H
#define __FILEMGMT_H

#include "cmdhandler.h"
#include <status.h>
#include <filesys.h>


class CFileManager;

// SPIFFS errors are larger than this
#define ERROR_TIMEOUT   -1
#define ERROR_TOO_BIG   -2


// For handling a file upload
class CFileUpload
{
  private:
    CFileManager& m_manager;
    String m_filename;
    CFileStream* m_file = nullptr;
    uint32_t m_size = 0;
    command_connection_t m_connection = nullptr;
    uint32_t m_written = 0;
    // SPIFFS error
    int m_error = ERROR_TIMEOUT;
    // Handles timeout condition
    Timer m_timer;

  private:

    void close();
    void endUpload();

  public:

    CFileUpload(CFileManager& manager, command_connection_t connection) :
      m_manager(manager),
      m_connection(connection)
    { }

    ~CFileUpload()
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
};


/** @brief  Callback function for file upload completion
 *
 * @param filename The file which has changed
 * @param error The SPIFFS error code
 */
typedef std::function<void(const CFileUpload& upload)> file_upload_callback_t;


class CFileManager: public CCommandHandler
{
    friend CFileUpload;

  private:
    CFileUpload* m_upload;
    file_upload_callback_t m_callback;

  private:
    ioerror_t getFile(command_connection_t connection, JsonObject& json);
    ioerror_t startUpload(command_connection_t connection, JsonObject& json);
    void endUpload();

  public:
    ~CFileManager();

    bool init();

    /* CCommandHandler */
    String getMethod() const;

    access_type_t minAccess() const
    {
      return access_admin;
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
};



#endif // __FILEMGMT_H
