/*
 * FirmwareUpdateManager.h
 *
 *  Created on: 13 May 2018
 *      Author: mikee47
 */

#ifndef __FIRMWARE_UPDATE_MANAGER_H
#define __FIRMWARE_UPDATE_MANAGER_H

#include "WString.h"
#include "SimpleTimer.h"
#include "../rboot/appcode/rboot-api.h"
#include "../fwpack/fwpack.h"

#include "CommandHandler.h"

enum __attribute__((packed)) fwupdate_state_t {
	update_started,
	update_ready,
	update_timeout
};

class FirmwareUpdateManager;

/*
 * Manages transfer of firmware into flash.
 *
 * An instance of this class is created when a valid header is received.
 *
 */
class FirmwareUpdateSession
{
public:
	FirmwareUpdateSession(FirmwareUpdateManager& manager, WSCommandConnection* connection) :
		m_manager(manager),
			m_connection(connection)
	{
	}

	void init(uint32_t imageSize, unsigned chunkSize);
	bool handleData(uint8_t* data, size_t size);
	ioerror_t apply();

	WSCommandConnection* connection() const
	{
		return m_connection;
	}

private:
	FirmwareUpdateManager& m_manager;
	WSCommandConnection* m_connection = nullptr;
	// Size of firmware image
	uint32_t m_imageSize = 0;
	// Data transferred in chunks
	unsigned m_chunkSize = 0;
	// Amount of firmware data written
	uint32_t m_bytesReceived = 0;
	// Indicates firmware has been received and checked, ready to be applied
	bool m_firmwareReady = false;
	// Detect timeout during transfer
	SimpleTimer m_timer;
	// RBOOT slot for update
	uint8_t m_slot = 0xFF;
	rboot_write_status m_rboot_status;
	// Encryption
	ghash_token_t m_tag;  // from header
	GCM m_gcm;

	void notify(request_status_t status, ioerror_t err);
	void uploadTimeout();
};


class FirmwareUpdateManager: public WSCommandHandler
{
	friend class FirmwareUpdateSession;

public:

	String getMethod() const;

	/*
	 * Update initialisation, etc. are handled via standare JSON messages.
	 */
	void handleMessage(WSCommandConnection* connection, JsonObject& json);

	/*
	 * Encrypted firmware is uploaded via binary channel.
	 */
	bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size);

private:
	FirmwareUpdateSession* m_session;

	bool checkSession(WSCommandConnection* connection);
	void deleteSession();
	ioerror_t startUpload(WSCommandConnection* connection, uint32_t imageSize, unsigned chunkSize);
	void uploadTimeout();
};

#endif // __FIRMWARE_UPDATE_MANAGER_H
