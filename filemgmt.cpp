/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#include "filemgmt.h"

#include <WString_P.h>
#include <status.h>

static DEFINE_STRING_P(METHOD_SPIFFS, "spiffs")

static DEFINE_STRING_P(COMMAND_LIST, "list")
static DEFINE_STRING_P(ATTR_FILES, "files")
static DEFINE_STRING_P(COMMAND_GET, "get")
static DEFINE_STRING_P(COMMAND_UPLOAD, "upload")
static DEFINE_STRING_P(COMMAND_DELETE, "delete")
static DEFINE_STRING_P(ATTR_SIZE, "size")
static DEFINE_STRING_P(ATTR_WRITTEN, "written")
static DEFINE_STRING_P(ATTR_BLOCKS, "blocks")
static DEFINE_STRING_P(ATTR_TOTAL, "total")
static DEFINE_STRING_P(ATTR_USED, "used")
static DEFINE_STRING_P(COMMAND_CHECK, "check")


#define UPLOAD_TIMEOUT_MS 2000

// SPIFFS errors are larger than this
#define ERROR_TIMEOUT   -1
#define ERROR_TOO_BIG   -2


CFileUpload::CFileUpload(CFileManager& manager, command_connection_t connection, String filename, size_t size) :
  m_manager(manager),
  m_name(filename),
  m_stream(filename),
  m_size(size),
  m_connection(connection),
  m_error(ERROR_TIMEOUT)
{
  m_timer.initializeMs(UPLOAD_TIMEOUT_MS, TimerDelegate(&CFileUpload::endUpload, this));
  m_timer.startOnce();
}

bool CFileUpload::handleData(command_connection_t connection, uint8_t* data, size_t size)
{
  if (m_connection != connection)
    return false;

  debug_i("%s(%u)", __FUNCTION__, size);

  m_timer.stop();

  size_t n = m_stream.write(data, size);
  if (n != size) {
    debug_e("File write error");
    m_error = int(n);
  }
  else {
    m_written += size;
    // Need more data
    if (m_written < m_size) {
      m_timer.startOnce();
      return true;
    }

    m_error = (m_written == m_size) ? SPIFFS_OK : ERROR_TOO_BIG;
  }

  endUpload();
  return true;
}


void CFileUpload::endUpload()
{
  if (m_connection) {
    DynamicJsonBuffer buffer;
    JsonObject& json = buffer.createObject();
    json[ATTR_METHOD()] = METHOD_SPIFFS();
    json[ATTR_COMMAND()] = COMMAND_UPLOAD();
    json[ATTR_NAME()] = m_name;
    json[ATTR_SIZE()] = m_size;
    json[ATTR_WRITTEN()] = m_written;
    if (m_error)
      setError(json, m_error);
    else
      setSuccess(json);
    m_connection->send(json);
  }

  m_manager.endUpload();
}


CFileManager::~CFileManager()
{
  endUpload();
}


void CFileManager::endUpload()
{
  if (m_upload) {
    delete m_upload;
    m_upload = nullptr;
  }
}

static void listFiles(JsonObject& json)
{
  JsonArray& files = json.createNestedArray(ATTR_FILES());

  spiffs_DIR d;
  spiffs_dirent info;

  SPIFFS_opendir(&_filesystemStorageHandle, "/", &d);
  while (SPIFFS_readdir(&d, &info)) {
    JsonObject& file = files.createNestedObject();
    file[ATTR_NAME()] = String((char*)info.name);
    file[ATTR_SIZE()] = info.size;
  }
  SPIFFS_closedir(&d);

  setSuccess(json);
}


static int deleteFile(const char* filename)
{
  if (filename == nullptr)
    return SPIFFS_ERR_NOT_A_FILE;

  return SPIFFS_remove(&_filesystemStorageHandle, filename);
}

static void deleteFiles(JsonObject& json)
{
  JsonArray& files = json[ATTR_FILES()];
  for (unsigned i = 0; i < files.size(); ++i) {
    JsonObject& file = files[i];
    int err = deleteFile(file[ATTR_NAME()]);
    if (err == SPIFFS_OK)
      setSuccess(file);
    else
      setError(file, err);
  }
}


static void getInfo(JsonObject& json)
{
  u32_t total, used;
  int err = SPIFFS_info(&_filesystemStorageHandle, &total, &used);
  if (err) {
    setError(json, err);
    return;
  }
  JsonObject& blocks = json.createNestedObject(ATTR_BLOCKS());
  blocks[ATTR_TOTAL()] = total;
  blocks[ATTR_USED()] = used;
  setSuccess(json);
}

static void check(JsonObject& json)
{
  setError(json, ioe_not_impl);
/*
  //!! Causes alignment exception. Not investigated.

  int err = SPIFFS_check(&_filesystemStorageHandle);
  if (err)
    setError(json, err);
  else
    setSuccess(json);
*/
}


String CFileManager::getMethod() const
{
  return METHOD_SPIFFS();
}



/*
 * For this to work WebSocketConnection needs some rework. We need to implement
 * a stream object which emits source data as websocket fragments.
 */
void CFileManager::sendFile(command_connection_t connection, String filename)
{
/*
  FileStream fs(filename);
  if (fs.available() <= 0)
    return;

  wsFrameType ft = WS_BINARY_FRAME;
  bool fin = false;
  do {
    char buffer[1024];
    uint16_t length = fs.readMemoryBlock(buffer, sizeof(buffer));
    fin = fs.isFinished();
    connection->send(buffer, length, ft, fin);
    ft = WS_CONTINUATION_FRAME;
  } while (!fin);
*/
}

void CFileManager::handleMessage(command_connection_t connection, JsonObject& json)
{
  endUpload();

  const char* cmd = json[ATTR_COMMAND()];
  if (COMMAND_GET() == cmd) {
    setError(json, ioe_not_impl);
/*
    const char* name = json[ATTR_NAME()];
    if (name)
      sendFile(connection, name);
    else
      setError(json, ioe_bad_param);
*/
  }
  else if (COMMAND_UPLOAD() == cmd) {
    const char* name = json[ATTR_NAME()];
    size_t size = json[ATTR_SIZE()];
    if (name && size) {
      m_upload = new CFileUpload(*this, connection, name, size);
      debug_i("File upload '%s', %u bytes", name, size);
      setPending(json);
    }
    else
      setError(json, ioe_bad_param);
  }
  else if (COMMAND_LIST() == cmd)
    listFiles(json);
  else if (COMMAND_DELETE() == cmd)
    deleteFiles(json);
  else if (COMMAND_INFO() == cmd)
    getInfo(json);
  else if (COMMAND_CHECK() == cmd)
    check(json);
  else
    CCommandHandler::handleMessage(connection, json);
}



