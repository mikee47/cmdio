/*
 * files.h
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#ifndef __FILEMGMT_H
#define __FILEMGMT_H

#include "cmdhandler.h"


class CFileManager;

// For handling a file upload
class CFileUpload
{
  private:
    CFileManager& m_manager;
    String m_name;
    FileOutputStream m_stream;
    uint32_t m_size;
    command_connection_t m_connection = nullptr;
    uint32_t m_written = 0;
    // SPIFFS error
    int m_error;
    Timer m_timer;

  private:

    void endUpload();

  public:

    CFileUpload(CFileManager& manager, command_connection_t connection, String filename, size_t size);

    bool handleData(command_connection_t connection, uint8_t* data, size_t size);
};


/** @brief  Callback function for firmware update
 *
 * Notifies application of update progress.
 */
typedef void (*fileman_callback_t)(command_connection_t connection, JsonObject& json);


class CFileManager: public CCommandHandler
{
    friend CFileUpload;

  private:
    CFileUpload* m_upload;

  private:
    void sendFile(command_connection_t connection, String filename);
    void endUpload();

  public:
    ~CFileManager();

    /* CCommandHandler */
    String getMethod() const;

    access_type_t minAccess() const
    {
      return access_admin;
    }

    void handleMessage(command_connection_t connection, JsonObject& json);

    bool handleData(command_connection_t connection, uint8_t* data, size_t size)
    {
      return m_upload ? m_upload->handleData(connection, data, size) : false;
    }
};



#endif // __FILEMGMT_H
