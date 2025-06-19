/*
 * FirmwareUpdateManager.cpp
 *
 *  Created on: 13 May 2018
 *      Author: mikee47
 *
 * Binary websocket messages are used for OTA firmware updating. Firmware is
 * encrypted and signed. We authenticate the first packet received; if this
 * fails we abort so flash is never touched.
 *
 * Setup and status are communicated separately - these don't need to be
 * encrypted but they aren't public.
 *
 * A separate C++ program is used to encrypt a firmware image.
 *
 */

#include "include/cmdio/FirmwareUpdateManager.h"
#include <Storage/SpiFlash.h>

// Notify upload progress increment in bytes
#define PROGRESS_NOTIFY_INCREMENT 16384 //8192

// Timeout if no data received for a period
#define FWUPDATE_TIMEOUT_MS 4000

// Validation of upload parameters
#define MIN_IMAGE_SIZE (100 * 1024)
#define MIN_CHUNK_SIZE (2 * 1024)
#define MAX_CHUNK_SIZE (128 * 1024)

DEFINE_FSTR_LOCAL(METHOD_FWUPDATE, "fwupdate");
DEFINE_FSTR_LOCAL(COMMAND_UPLOAD, "upload");

DEFINE_FSTR_LOCAL(ATTR_IMAGESIZE, "imagesize");
DEFINE_FSTR_LOCAL(ATTR_CHUNKSIZE, "chunksize");
DEFINE_FSTR_LOCAL(COMMAND_APPLY, "apply");
DEFINE_FSTR_LOCAL(COMMAND_CANCEL, "cancel");
DEFINE_FSTR_LOCAL(ATTR_SAVE, "save");

DEFINE_FSTR_LOCAL(COMMAND_QUERY, "query")
DEFINE_FSTR(ATTR_RUN_PART, "RunPart")
DEFINE_FSTR(ATTR_BOOT_PART, "BootPart")

DEFINE_FSTR_LOCAL(FLASHIP_FILENAME, "config/fip.bin")

/* FirmwareUpdateSession */

FirmwareUpdateSession::FirmwareUpdateSession(FirmwareUpdateManager& manager, WSCommandConnection* connection,
											 uint32_t imageSize, unsigned chunkSize)
	: manager(manager), connection(connection), imageSize(imageSize), chunkSize(chunkSize)
{
#ifdef ENABLE_FLASHIP
	partition = *Storage::findPartition(Storage::Partition::Type::app);
#else
	// select rom slot to flash
	partition = OtaManager.getNextBootPartition();
#endif

	timer.initializeMs<FWUPDATE_TIMEOUT_MS>([this]() {
		notify(IO::Error::timeout);
		this->manager.session.reset();
	});
	timer.startOnce();

	debug_i("[FW] image size %u, ROM %s", imageSize, partition.name().c_str());
}

void FirmwareUpdateSession::notify(IO::ErrorCode err, const String& text)
{
	if(!WSCommandConnection::isActive(connection)) {
		return;
	}

	StaticJsonDocument<256> doc;
	auto json = doc.to<JsonObject>();
	json[ATTR_METHOD] = METHOD_FWUPDATE;
	json[ATTR_COMMAND] = COMMAND_UPLOAD;
	json[ATTR_IMAGESIZE] = bytesReceived;
	IO::setError(json, err, text);
	connection->send(json);
}

/*
 * Called from manager.
 */
IO::ErrorCode FirmwareUpdateSession::apply(bool save)
{
	if(!firmwareReady) {
		return IO::Error::bad_command;
	}
	stream.reset();

#ifdef ENABLE_FLASHIP
	if(!save) {
		return IO::Error::not_impl;
	}
	auto fip = new FlashIP;
	bool ret = fip && fip->addFile(partition, 0, FLASHIP_FILENAME);
	if(ret) {
		auto timer = new AutoDeleteTimer;
		timer->initializeMs<1000>([](void* param) { static_cast<FlashIP*>(param)->execute(); }, fip);
		timer->startOnce();
	}
#else
	bool ret = OtaManager.setBootPartition(partition, save);
#endif

	// Take a look at rboot_set_current_rom - only fails if malloc does
	return ret ? IO::Error::success : IO::Error::no_mem;
}

bool FirmwareUpdateSession::handleHeader(uint8_t* data, size_t size)
{
	if(size < sizeof(firmware_header_t)) {
		debug_e("[FW] Header packet too small %u", size);
		notify(IO::Error::bad_size);
		return false;
	}

	// Data can be anywhere in message payload so need to do this to avoid alignment exceptions
	firmware_header_t header;
	memcpy(&header, data, sizeof(header));

	crypt_key_t deviceKey;
	memcpy_P(&deviceKey, manager.deviceKey.data(), sizeof(deviceKey));

	if(!beginDecrypt(gcm, header, deviceKey)) {
		debug_w("[FW] Bad header");
		notify(IO::Error::bad_checksum);
		return false;
	}

	debug_i("[FW] Header OK");

	if(imageSize != sizeof(firmware_header_t) + header.encrypted.imageSize) {
		// Didn't match size in original command
		debug_w("[FW] Bad image size");
		notify(IO::Error::bad_size);
		return false;
	}

	bytesReceived = sizeof(firmware_header_t);
	tag = header.tag;

#ifdef ENABLE_FLASHIP
	fileDelete(FLASHIP_FILENAME);
	stream = std::make_unique<FileStream>(FLASHIP_FILENAME, File::CreateNewAlways | File::WriteOnly);
#else
	if(partition.size() < imageSize) {
		debug_e("[FW] Partition too small %u", partition.size());
		notify(IO::Error::bad_size);
		return false;
	}

	stream = std::make_unique<Storage::PartitionStream>(partition, Storage::Mode::BlockErase);
#endif

	data += sizeof(firmware_header_t);
	size -= sizeof(firmware_header_t);

	return (size == 0) ? true : handlePayload(data, size);
}

bool FirmwareUpdateSession::handleData(uint8_t* data, size_t size)
{
	timer.stop();

	if(!stream) {
		return handleHeader(data, size);
	}

	return handlePayload(data, size);
}

bool FirmwareUpdateSession::handlePayload(uint8_t* data, size_t size)
{
	if(!stream) {
		return false;
	}

	gcm.decrypt(data, size);

	if(stream->write(data, size) != size) {
		debug_w("[FW] Write error");
		notify(IO::Error::file);
		return false;
	}

	bytesReceived += size;

	if(bytesReceived < imageSize) {
		// Notify at end of each chunk
		if(bytesReceived % chunkSize == 0) {
			System.queueCallback([this]() { notify(IO::Error::pending); });
		}

		timer.startOnce();
		return true;
	}

	// Do a final authentication on the payload
	if(tag != gcm.computeTag()) {
		notify(IO::Error::bad_config);
		debug_w("[FW] Tag FAIL");
		return false;
	}

	notify(IO::Error::success);
	firmwareReady = true;
	return true;
}

/* FirmwareUpdateManager */

void FirmwareUpdateManager::startup()
{
	fileDelete(FLASHIP_FILENAME);
}

/*
 * Verify that a session is active and belongs to the specified connection.
 * This allows an upload session to be locked to the session which started it
 * and prevents interruption (accidental or otherwise) from a different connection.
 */
bool FirmwareUpdateManager::checkSession(WSCommandConnection* connection)
{
	// Active session ?
	if(!session) {
		return false;
	}

	// Same connection ?
	if(session->getConnection() != connection) {
		debug_e("[FW] Connection mismatch");
		return false;
	}

	// Session appears OK
	return true;
}

IO::ErrorCode FirmwareUpdateManager::startUpload(WSCommandConnection* connection, uint32_t imageSize,
												 unsigned chunkSize)
{
	session.reset();

	// Validate parameters
	if(imageSize < MIN_IMAGE_SIZE) {
		debug_w("[FW] Bad image size %u", imageSize);
		return IO::Error::bad_param;
	}

	if(chunkSize < MIN_CHUNK_SIZE || chunkSize > MAX_CHUNK_SIZE) {
		debug_w("[FW] Bad chunk size %u", chunkSize);
		return IO::Error::bad_param;
	}

#ifndef ENABLE_FLASHIP
	if(OtaManager.getNextBootPartition() == OtaManager.getRunningPartition()) {
		return IO::Error::access_denied;
	}
#endif

	session = std::make_unique<FirmwareUpdateSession>(*this, connection, imageSize, chunkSize);
	return session ? IO::Error::pending : IO::Error::no_mem;
}

String FirmwareUpdateManager::getMethod() const
{
	return METHOD_FWUPDATE;
}

void FirmwareUpdateManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];

	if(COMMAND_UPLOAD == command) {
		auto err = startUpload(connection, json[ATTR_IMAGESIZE], json[ATTR_CHUNKSIZE]);
		json[ATTR_IMAGESIZE] = 0;
		json.remove(ATTR_CHUNKSIZE);
		if(err) {
			IO::setError(json, err);
		} else {
			json[DONT_RESPOND] = true;
		}
		return;
	}

	if(COMMAND_QUERY == command) {
#ifndef ENABLE_FLASHIP
		json[ATTR_RUN_PART] = OtaManager.getRunningPartition().name();
		json[ATTR_BOOT_PART] = OtaManager.getBootPartition().name();
#endif
		IO::setSuccess(json);
		return;
	}

#ifndef ENABLE_FLASHIP
	if(COMMAND_APPLY == command && json[ATTR_SAVE]) {
		auto runpart = OtaManager.getRunningPartition();
		auto bootpart = OtaManager.getBootPartition();
		if(runpart != bootpart && OtaManager.setBootPartition(runpart, true)) {
			IO::setSuccess(json);
			return;
		}
	}
#endif

	IO::ErrorCode err;
	if(!checkSession(connection)) {
		err = IO::Error::bad_command;
	} else if(COMMAND_APPLY == command) {
		err = session->apply(json[ATTR_SAVE]);
	} else if(COMMAND_CANCEL == command) {
		err = IO::Error::success;
	} else {
		err = IO::Error::bad_command;
	}

	session.reset();

	if(err) {
		IO::setError(json, err);
	} else {
		IO::setSuccess(json);
	}
}

bool FirmwareUpdateManager::handleData(WSCommandConnection* connection, uint8_t* data, size_t size)
{
	if(!checkSession(connection)) {
		return false;
	}

	if(!session->handleData(data, size)) {
		session.reset();
		return false;
	}

	return true;
}
