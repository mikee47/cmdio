/*
 * fwupdate.h
 *
 *  Created on: 13 May 2018
 *      Author: Mike
 */

#ifndef __FWUPDATE_H
#define __FWUPDATE_H

#include <SmingCore/SmingCore.h>
#include <appcode/rboot-api.h>
#include <fwpack.h>

#include "cmdhandler.h"


enum __attribute__((packed)) fwupdate_state_t {
  update_started,
  update_ready,
  update_timeout
};


class CFirmwareUpdateManager;

/*
 * Manages transfer of firmware into flash.
 *
 * An instance of this class is created when a valid header is received.
 *
 */
class CFirmwareUpdateSession
{
  private:
    CFirmwareUpdateManager& m_manager;
    command_connection_t m_connection = nullptr;
    // Size of firmware image
    uint32_t m_imageSize = 0;
    // Data transferred in chunks
    unsigned m_chunkSize = 0;
    // Amount of firmware data written
    uint32_t m_bytesReceived = 0;
    // Indicates firmware has been received and checked, ready to be applied
    bool m_firmwareReady = false;
    // Detect timeout during transfer
    Timer m_timer;
    // RBOOT slot for update
    uint8_t m_slot = 0xFF;
    rboot_write_status m_rboot_status;
    // Encryption
    ghash_token_t m_tag; // from header
    GCM m_gcm;

    void notify(request_status_t status, ioerror_t err);
    void uploadTimeout();

  public:
    CFirmwareUpdateSession(CFirmwareUpdateManager& manager, command_connection_t connection) :
      m_manager(manager),
      m_connection(connection)
    { }

    void init(uint32_t imageSize, unsigned chunkSize);
    bool handleData(uint8_t* data, size_t size);
    ioerror_t apply();

    command_connection_t connection() const
    {
      return m_connection;
    }
};


class CFirmwareUpdateManager: public CCommandHandler
{
    friend class CFirmwareUpdateSession;

  private:
    CFirmwareUpdateSession* m_session;

    bool checkSession(command_connection_t connection);
    void deleteSession();
    ioerror_t startUpload(command_connection_t connection, uint32_t imageSize, unsigned chunkSize);
    void uploadTimeout();

  public:

    String getMethod() const;

    /*
     * Update initialisation, etc. are handled via standare JSON messages.
     */
    void handleMessage(command_connection_t connection, JsonObject& json);

    /*
     * Encrypted firmware is uploaded via binary channel.
     */
    bool handleData(command_connection_t connection, uint8_t* data, size_t size);
};

#endif // __FWUPDATE_H
