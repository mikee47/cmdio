/*
 * fwupdate.cpp
 *
 *  Created on: 13 May 2018
 *      Author: Mike
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


#include <fwupdate.h>
#include <status.h>


// Notify upload progress increment in bytes
#define PROGRESS_NOTIFY_INCREMENT 16384   //8192

// Timeout if no data received for a period
#define FWUPDATE_TIMEOUT_MS   2000

// Validation of upload parameters
#define MIN_IMAGE_SIZE  (250 * 1024)
#define MAX_IMAGE_SIZE  (500 * 1024)
#define MIN_CHUNK_SIZE  (2 * 1024)
#define MAX_CHUNK_SIZE  (128 * 1024)

static DEFINE_STRING_P(METHOD_FWUPDATE, "fwupdate")
static DEFINE_STRING_P(COMMAND_UPLOAD, "upload")
static DEFINE_STRING_P(ATTR_IMAGESIZE, "imagesize")
static DEFINE_STRING_P(ATTR_CHUNKSIZE, "chunksize")
static DEFINE_STRING_P(COMMAND_APPLY, "apply")
static DEFINE_STRING_P(COMMAND_CANCEL, "cancel")


//TODO: This needs to be stored in a fixed location unaffected by firmware updating
static const crypt_key_t PROGMEM g_deviceKey = {
  .u8 = { 0x48, 0xE0, 0x67, 0xE6, 0xA0, 0xA5, 0x5A, 0x0E, 0x2A, 0x0C, 0xB0, 0xB7, 0xF8, 0x8B, 0xC3, 0x5B }
};



/* CFirmwareUpdateSession */


void CFirmwareUpdateSession::init(uint32_t imageSize, unsigned chunkSize)
{
  // select rom slot to flash
  rboot_config bootconf = rboot_get_config();
  // Write new firmware to inactive slot
  m_slot = (bootconf.current_rom == 0) ? 1 : 0;

  // Initialise firmware update at ROM slot address
  m_rboot_status = rboot_write_init(bootconf.roms[m_slot]);
  m_imageSize = imageSize;
  m_chunkSize = chunkSize;
  m_bytesReceived = 0;
  notify(status_pending, ioe_success);

  m_timer.setCallback([](void* arg) {
    reinterpret_cast<CFirmwareUpdateSession*>(arg)->uploadTimeout();
  }, this);
  m_timer.startMs(FWUPDATE_TIMEOUT_MS);

  debug_i("%s: upload %u bytes to ROM %u @ 0x%08X", __PRETTY_FUNCTION__, m_imageSize, m_slot, bootconf.roms[m_slot]);
}


void CFirmwareUpdateSession::notify(request_status_t status, ioerror_t err)
{
  if (m_connection) {
    DynamicJsonBuffer buffer;
    JsonObject& json = buffer.createObject();
    json[ATTR_METHOD()] = METHOD_FWUPDATE();
    json[ATTR_COMMAND()] = COMMAND_UPLOAD();
    json[ATTR_IMAGESIZE()] = m_bytesReceived;

    if (status == status_pending)
      setPending(json);
    else if (status == status_success)
      setSuccess(json);
    else
      setError(json, err);

    m_connection->send(json);
  }
}


/*
 * Called asynchronously via Timer.
 */
void CFirmwareUpdateSession::uploadTimeout()
{
  notify(status_error, ioe_timeout);
  // We're done
  m_manager.deleteSession();
}


/*
 * Called from manager.
 */
ioerror_t CFirmwareUpdateSession::apply()
{
  if (!m_firmwareReady)
    return ioe_bad_command;

  bool ret = rboot_set_current_rom(m_slot);

  // Take a look at rboot_set_current_rom - only fails if malloc does
  return ret ? ioe_success : ioe_nomem;
}


/*
 * Return true on success, false on error and session will be deleted.
 */
bool CFirmwareUpdateSession::handleData(uint8_t* data, size_t size)
{
  m_timer.stop();

  // Header appears at start of data
  if (m_bytesReceived == 0) {
   if (size < sizeof(firmware_header_t)) {
      debug_e("%s: Header packet too small", __FUNCTION__);
      return false;
    }

    // Data can be anywhere in message payload so need to do this to avoid alignment exceptions
    firmware_header_t header;
    memcpy(&header, data, sizeof(header));

    crypt_key_t devkey;
    memcpy_P(&devkey, &g_deviceKey, sizeof(devkey));

    if (!beginDecrypt(m_gcm, header, devkey)) {
      debug_w("%s: Header authentication failed", __FUNCTION__);
      return false;
    }

    debug_i("%s: Header authenticated", __FUNCTION__);

    if (m_imageSize != sizeof(firmware_header_t) + header.encrypted.imageSize) {
      debug_w("%s: Firmware image size invalid");
      return false;
    }

    data += sizeof(firmware_header_t);
    size -= sizeof(firmware_header_t);

    m_bytesReceived = sizeof(firmware_header_t);
    m_tag = header.tag;
  }

  m_bytesReceived += size;

  if (m_bytesReceived > m_imageSize) {
    debug_w("%s(): Extra bytes at end of payload", __FUNCTION__);
    return false;
  }

  m_gcm.decrypt(data, size);

  // Data transfer
  bool write_ok = rboot_write_flash(&m_rboot_status, data, size);

  if (write_ok) {
    if (m_bytesReceived < m_imageSize) {
      // Notify at end of each chunk
      if (m_bytesReceived % m_chunkSize == 0)
        notify(status_pending, ioe_success);

      m_timer.startMs(FWUPDATE_TIMEOUT_MS);
      return true;
    }

    if (m_bytesReceived == m_imageSize) {
      // Do a final authentication on the payload
      if (m_tag != m_gcm.computeTag()) {
        notify(status_error, ioe_bad_config);
        debug_w("%s: Tag FAIL", __FUNCTION__);
        return false;
      }

      if (rboot_write_end(&m_rboot_status)) {
        notify(status_success, ioe_success);
        m_firmwareReady = true;
        return true;
      }
    }
  }

  notify(status_error, ioe_nomem);
  return false;
}


/* CFirmwareUpdateManager */


/*
 * Verify that a session is active and belongs to the specified connection.
 * This allows an upload session to be locked to the session which started it
 * and prevents interruption (accidental or otherwise) from a different connection.
 */
bool CFirmwareUpdateManager::checkSession(command_connection_t connection) {
  // Active session ?
  if (m_session == nullptr) {
    debug_e("No active upload session");
    return false;
  }

  // Same connection ?
  if (m_session->connection() != connection) {
    debug_e("Connection mismatch");
    return false;
  }

  // Session appears OK
  return true;
}


void CFirmwareUpdateManager::deleteSession()
{
  if (m_session) {
    delete m_session;
    m_session = nullptr;
  }
}



ioerror_t CFirmwareUpdateManager::startUpload(command_connection_t connection, uint32_t imageSize, unsigned chunkSize)
{
  deleteSession();

  // Validate parameters
  if (imageSize < MIN_IMAGE_SIZE || imageSize > MAX_IMAGE_SIZE) {
    debug_w("Invalid image size %u", imageSize);
    return ioe_bad_param;
  }

  if (chunkSize < MIN_CHUNK_SIZE || chunkSize > MAX_CHUNK_SIZE) {
    debug_w("Invalid chunk size %u", chunkSize);
    return ioe_bad_param;
  }

  m_session = new CFirmwareUpdateSession(*this, connection);
  if (!m_session)
    return ioe_nomem;

  m_session->init(imageSize, chunkSize);

  return ioe_success;
}


String CFirmwareUpdateManager::getMethod() const
{
  return METHOD_FWUPDATE();
}


void CFirmwareUpdateManager::handleMessage(command_connection_t connection, JsonObject& json)
{
  const char* command = json[ATTR_COMMAND()];

  if (COMMAND_UPLOAD() == command) {
    ioerror_t err = startUpload(connection, json[ATTR_IMAGESIZE()], json[ATTR_CHUNKSIZE()]);
    if (err)
      setError(json, err);
    else
      json[DONT_RESPOND()] = true;
    return;
  }

  ioerror_t err;
  if (!checkSession(connection))
    err = ioe_bad_command;
  else if (COMMAND_APPLY() == command)
    err = m_session->apply();
  else if (COMMAND_CANCEL() == command)
    err = ioe_success;
  else
    err = ioe_bad_command;

  deleteSession();

  if (err)
    setError(json, err);
  else
    setSuccess(json);
}



bool CFirmwareUpdateManager::handleData(command_connection_t connection, uint8_t* data, size_t size)
{
//  debug_i("%s(%u)", __FUNCTION__, size);
  if (!checkSession(connection))
    return false;

  if (!m_session->handleData(data, size)) {
    deleteSession();
    return false;
  }

  return true;
}



