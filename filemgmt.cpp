/*
 * files.cpp
 *
 *  Created on: 28 May 2018
 *      Author: Mike
 */

#include "filemgmt.h"

#include <WString_P.h>
#include <status.h>
#include <hybridfile.h>

static DEFINE_STRING_P(METHOD_FILES, "files")

static DEFINE_STRING_P(COMMAND_LIST, "list")
static DEFINE_STRING_P(ATTR_FILES, "files")
static DEFINE_STRING_P(COMMAND_GET, "get")
static DEFINE_STRING_P(COMMAND_UPLOAD, "upload")
static DEFINE_STRING_P(COMMAND_DELETE, "delete")
static DEFINE_STRING_P(ATTR_SIZE, "size")
static DEFINE_STRING_P(ATTR_FLAGS, "flags")
static DEFINE_STRING_P(ATTR_MTIME, "mtime")
static DEFINE_STRING_P(ATTR_WRITTEN, "written")
static DEFINE_STRING_P(ATTR_BLOCKS, "blocks")
static DEFINE_STRING_P(ATTR_TOTAL, "total")
static DEFINE_STRING_P(ATTR_USED, "used")
static DEFINE_STRING_P(COMMAND_CHECK, "check")
static DEFINE_STRING_P(COMMAND_FORMAT, "format")

// This is DWORD aligned so we can access it directly
extern const PROGMEM uint8_t __fwfiles_data[];

#define UPLOAD_TIMEOUT_MS 2000


void getMeta(JsonObject& json, filestream_t f)
{
  file_meta_t meta;
  if (f->getMeta(meta)) {
    json[ATTR_ACCESS()] = meta.accessStr();
    json[ATTR_FLAGS()] = meta.flagsStr();
    json[ATTR_MTIME()] = meta.mtime;
  }
}


bool CFileUpload::init(const char* filename, size_t size)
{
  m_filename = filename;
  m_size = size;
  m_file = openFile(m_filename, eFO_CreateNewAlways | eFO_WriteOnly);
  if (!m_file)
    return false;

  m_error = ERROR_TIMEOUT;
  m_timer.initializeMs(UPLOAD_TIMEOUT_MS, TimerDelegate(&CFileUpload::endUpload, this));
  m_timer.startOnce();

  return true;
}


void CFileUpload::close()
{
  if (m_file) {
    delete m_file;
    m_file = nullptr;
  }
}


bool CFileUpload::handleData(command_connection_t connection, uint8_t* data, size_t size)
{
  if (m_connection != connection)
    return false;

  debug_i("%s(%u)", __FUNCTION__, size);

  m_timer.stop();

  size_t n = m_file->write(data, size);
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
  if (m_file)
    m_file->flush();

  if (m_connection) {
    DynamicJsonBuffer buffer;
    JsonObject& json = buffer.createObject();
    json[ATTR_METHOD()] = METHOD_FILES();
    json[ATTR_COMMAND()] = COMMAND_UPLOAD();
    json[ATTR_NAME()] = m_filename;
    json[ATTR_SIZE()] = m_size;
    json[ATTR_WRITTEN()] = m_written;
    getMeta(json, m_file);
    if (m_error)
      setError(json, m_error);
    else
      setSuccess(json);
    m_connection->send(json);
  }
  close();

  m_manager.endUpload();
}



/* CFileManager */


CFileManager::~CFileManager()
{
  endUpload();
}


static s32_t api_spiffs_read(u32_t addr, u32_t size, u8_t *dst)
{
  flashmem_read(dst, addr, size);
  return SPIFFS_OK;
}

static s32_t api_spiffs_write(u32_t addr, u32_t size, u8_t *src)
{
  //debugf("api_spiffs_write");
  flashmem_write(src, addr, size);
  return SPIFFS_OK;
}

static s32_t api_spiffs_erase(u32_t addr, u32_t size)
{
  debugf("api_spiffs_erase");
  u32_t sect_first = flashmem_get_sector_of_address(addr);
  u32_t sect_last = sect_first;
  while (sect_first <= sect_last)
    if (!flashmem_erase_sector(sect_first++))
      return SPIFFS_ERR_INTERNAL;

  return SPIFFS_OK;
}

bool CFileManager::init()
{
  if (filesys)
    delete filesys;

  auto freeheap = system_get_free_heap_size();
  debug_i("1: free heap = %u", freeheap);
  auto fs = new CHybridFileSystem();
  debug_i("2: heap used = %u", freeheap - system_get_free_heap_size());
  filesys = fs;
  if (!fs)
    return false;

  spiffs_config cfg = spiffs_get_storage_config();
  cfg.hal_read_f = api_spiffs_read;
  cfg.hal_write_f = api_spiffs_write;
  cfg.hal_erase_f = api_spiffs_erase;
  bool ret = fs->init(__fwfiles_data, cfg);

  debug_i("3: Heap used = %u", freeheap - system_get_free_heap_size());

  return ret;
}


void CFileManager::endUpload()
{
  if (m_upload) {
    if (m_callback)
      m_callback(*m_upload);
    delete m_upload;
    m_upload = nullptr;
  }
}


static JsonObject& findOrCreateFile(JsonArray& files, const String& name)
{
  for (auto& f: files)
    if (name == f[ATTR_NAME()])
      return f;

  auto& f = files.createNestedObject();
  f[ATTR_NAME()] = name;
  return f;
}

static void listFiles(JsonObject& json)
{
  auto& files = json.createNestedArray(ATTR_FILES());

  fileinfo_t fi = findFirstFile();
  if (fi) {
    do {
      auto& file = files.createNestedObject();
      file[ATTR_NAME()] = fi->name();
      file[ATTR_SIZE()] = fi->size();

      auto f = openFile(fi);
      if (f) {
        getMeta(file, f);
        delete f;
      }
    } while (findNextFile(fi));
    delete fi;
  }

  setSuccess(json);
}


static void deleteFiles(JsonObject& json)
{
  JsonArray& files = json[ATTR_FILES()];
  for (unsigned i = 0; i < files.size(); ++i) {
    JsonObject& file = files[i];
    if (deleteFile(file[ATTR_NAME()]))
      setSuccess(file);
    else
      setError(file);
  }
}


static void getInfo(JsonObject& json)
{
  /*
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
*/
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


/*
 * Reformat SPIFFS to blank state
 */
static void format(JsonObject& json)
{
  if (spiffs_format())
    setSuccess(json);
  else
    setError(json);
}


String CFileManager::getMethod() const
{
  return METHOD_FILES();
}



/*
 * TODO:
 *
 * Get a file using a websocket. This can only be used for small files but is
 * authenticated so can be used to obtain the contents of protected config files.
 *
 * For small config files we could integrate the JSON into the respopnse but this
 * is inefficient and restricts file sizes further. Sending the file via binary
 * connection is better but should probably have a header.
 *
 * It would probably be easier to authenticate HTTP connections using tokens.
 * This would be generated and returned in the websocket authentication
 * packet. Each websocket connection would need to remember it. If an HTTP
 * request requires authentication then the token would require matching
 * against all open websockets for a match; that would determine access level.
 *
 * The token could be as simple as the 32-bit connection ID, however it would
 * be wise to hash it for additional security. We have ghash available, but
 * as it's non-reversible we'd either need to compute on the fly or cache
 * at the expense of extra RAM. Without extra entropy the token can be too easily
 * created from the CID so it looks like we need to throw in some additional
 * random data before hashing.
 *
 * Note that we impose the condition that public connections are via HTTPS
 * to protect session data.
 *
 * To summarise: The purpose of a CID is to ensure only one websocket is kept
 * open per client browser, which is an optimisation/convenience function.
 * The purpose of a token is to enforce authentication, so must be secure.
 *
 * Token can be passed in URL, or as cookie but probably best as custom header
 * value.
 *
 */
ioerror_t CFileManager::getFile(command_connection_t connection, JsonObject& json)
{
  return setError(json, ioe_not_impl);
/*

  const char* name = json[ATTR_NAME()];
  if (!name) {
    setError(json, ioe_bad_param);
    return;
  }

  file_t fh = fileOpen(filename, eFO_ReadOnly);
  if (fh < 0) {
    return ioe_spiffs;

  }

  filestream_t fs = openFile(filename);
  if (!fs)
    return;

  wsFrameType ft = WS_BINARY_FRAME;
  bool fin = false;
  do {
    char buffer[1024];
    uint16_t length = fs.readMemoryBlock(buffer, sizeof(buffer));
    fin = fs.isFinished();
    connection->send(buffer, length, ft, fin);
    fs.seek(length);
    ft = WS_CONTINUATION_FRAME;
  } while (!fin);

  delete fs;
*/
}


ioerror_t CFileManager::startUpload(command_connection_t connection, JsonObject& json)
{
  const char* name = json[ATTR_NAME()];
  size_t size = json[ATTR_SIZE()];
  if (name == nullptr || size <= 0)
    return setError(json, ioe_bad_param);

  m_upload = new CFileUpload(*this, connection);
  if (m_upload == nullptr)
    return setError(json, ioe_nomem);

  if (!m_upload->init(name, size)) {
    delete m_upload;
    return setError(json, ioe_file);
  }

  debug_i("File upload '%s', %u bytes", name, size);
  setPending(json);
  return ioe_success;
}


void CFileManager::handleMessage(command_connection_t connection, JsonObject& json)
{
  endUpload();

  const char* cmd = json[ATTR_COMMAND()];
  if (COMMAND_GET() == cmd)
    getFile(connection, json);
  else if (COMMAND_UPLOAD() == cmd)
    startUpload(connection, json);
  else if (COMMAND_LIST() == cmd)
    listFiles(json);
  else if (COMMAND_DELETE() == cmd)
    deleteFiles(json);
  else if (COMMAND_INFO() == cmd)
    getInfo(json);
  else if (COMMAND_CHECK() == cmd)
    check(json);
  else if (COMMAND_FORMAT() == cmd)
    format(json);
  else
    CCommandHandler::handleMessage(connection, json);
}



