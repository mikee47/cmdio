/*
 * FirmwareUpdateManager.h
 *
 *  Created on: 13 May 2018
 *      Author: mikee47
 */

#pragma once

#include <WString.h>
#include <Timer.h>
#include <../../tools/fwpack/fwpack.h>
#include <cmdio/CommandHandler.h>
#include <Storage/PartitionStream.h>

#define ENABLE_FLASHIP

#ifdef ENABLE_FLASHIP
#include <FlashIP.h>
#else
#include <Ota/Manager.h>
#endif

DECLARE_FSTR(ATTR_RUN_PART)
DECLARE_FSTR(ATTR_BOOT_PART)

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
	FirmwareUpdateSession(FirmwareUpdateManager& manager, WSCommandConnection* connection, uint32_t imageSize,
						  unsigned chunkSize);

	bool handleData(uint8_t* data, size_t size);
	IO::ErrorCode apply(bool save);

	WSCommandConnection* getConnection() const
	{
		return connection;
	}

private:
	void notify(IO::ErrorCode err, const String& text = nullptr);
	bool handleHeader(uint8_t* data, size_t size);
	bool handlePayload(uint8_t* data, size_t size);

	FirmwareUpdateManager& manager;
	WSCommandConnection* connection;
	// Size of firmware image
	uint32_t imageSize{0};
	// Data transferred in chunks
	uint32_t chunkSize{0};
	// Amount of firmware data written
	uint32_t bytesReceived{0};
	// Indicates firmware has been received and checked, ready to be applied
	bool firmwareReady{false};
	// Detect timeout during transfer
	Timer timer;
	// Target partition
	std::unique_ptr<ReadWriteStream> stream;
	// Application partition for update
	Storage::Partition partition{};
	// Encryption
	ghash_token_t tag; // from header
	GCM gcm;
};

class FirmwareUpdateManager : public WSCommandHandler
{
	friend class FirmwareUpdateSession;

public:
	FirmwareUpdateManager(const FlashString& deviceKey) : deviceKey(deviceKey)
	{
	}

	void startup() override;

	String getMethod() const;

	/*
	 * Update initialisation, etc. are handled via standare JSON messages.
	 */
	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	PageInfo getPageInfo() const override
	{
		return {getAccess(), F("Firmware")};
	}

	/*
	 * Encrypted firmware is uploaded via binary channel.
	 */
	bool handleData(WSCommandConnection* connection, uint8_t* data, size_t size);

private:
	bool checkSession(WSCommandConnection* connection);
	IO::ErrorCode startUpload(WSCommandConnection* connection, uint32_t imageSize, unsigned chunkSize);

	const FlashString& deviceKey;
	std::unique_ptr<FirmwareUpdateSession> session;
};
