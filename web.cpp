/*
 * web.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */

#include <web.h>
#include <status.h>
#include <WString_P.h>
#include <sockmgr.h>
#include <network.h>

#include "filemgmt.h"


HttpServerEx* m_server;

DEFINE_STRING_P(FILE_INDEX_HTML, "index.html")
static DEFINE_STRING_P(FILE_CONFIG_HTML, "config.html")
static DEFINE_STRING_P(FILE_ERROR_HTML, "error.html")

static DEFINE_STRING_P(METHOD_WEB, "web")
static DEFINE_STRING_P(ATTR_PATH, "path")
static DEFINE_STRING_P(ATTR_CLIENTS, "clients")
static DEFINE_STRING_P(ATTR_SOCKETS, "sockets")


void subst(String& tmpl, const String& var, const String& value)
{
  String s = "{" + var + "}";
  tmpl.replace(s, value);
}


/*
 * All web file requests come here.
 *
 * File security is indicated if first character of filename is '.'.
 * The second character gives the minimum access level as defined
 * by access_type_t:
 *
 *  0 none
 *  1 guest
 *  2 user
 *  3 admin
 *
 * Any other character is treated as admin. Examples:
 *
 *  .config.json    admin
 *  .2config.json   user
 *  .1test.js       guest
 *
 * Purpose is to prevent over-exposure of scripts to introspection.
 * We also want to be able to freely view files at admin level.
 *
 * Would it be better to make security transparent? Typically an ACL
 * is implemented by the filing system using metadata. We could have
 * a file (e.g. .acl) which provides this information but that would
 * consume memory and resources. We could put the access level at the
 * end of the filename, e.g. "config.json.3". The request would simply
 * specify "config.json" so we'd need to check for all variants.
 *
 * Another alternative would be to modify the spiffs image tool so
 * it builds the access level into the image. SPIFFS provides optional
 * metadata space so it's ideal. We'd also to revise the websocket API
 * to incorporate this metadata. HTTP requests can put the metadata
 * into the response header.
 *
 * Maybe the simplest approach is to put the access just before the
 * file extension. That will preserve naming order and make things
 * a bit more readable.
 *
 */
int CWebServer::requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response)
{
  String file = request.getPath();
  file.toLowerCase();
#if DEBUG_BUILD
  IPAddress ip = connection.getRemoteIp();
  uint16_t port = connection.getRemotePort();
  debug_i("%s(%s[%u], '%s') from %s:%u", __FUNCTION__, http_method_str(request.method), request.method, file.c_str(), ip.toString().c_str(), port);
#endif

  if (file[0] == '/')
    file.remove(0, 1);

  if (file.length() == 0 || (WifiAccessPoint.isEnabled() && !filesys->exists(file)))
    file = FILE_INDEX_HTML();

//  WifiAccessPoint.isEnabled() ? FILE_CONFIG_HTML() : FILE_INDEX_HTML();

  sendFile(file, request.getQueryParameter(ATTR_CID()), response);

  // For errors construct and send error page
  if (response.code >= 400) {
    http_status status = static_cast<http_status>(response.code);
    String tmpl = getFileContent(FILE_ERROR_HTML());
    subst(tmpl, ATTR_PATH(), request.getPath());
    subst(tmpl, ATTR_CODE(), String(status));
    subst(tmpl, ATTR_TEXT(), httpGetStatusText(status));
    response.setContentType(MIME_HTML);
    response.sendString(tmpl);

/*
    TemplateFileStream *tmpl = new TemplateFileStream(FILE_ERROR_HTML());
    auto &vars = tmpl->variables();
    vars[ATTR_PATH()] = request.getPath();
    vars[ATTR_CODE()] = status;
    vars[ATTR_TEXT()] = text;
    response.sendTemplate(tmpl);
*/
  }

  return 0;
}



bool CWebServer::sendFile(const String& filename, const String& cid, HttpResponse& response)
{
  auto cc = socketManager.findConnection(cid.c_str());
  access_type_t access = cc ? cc->access() : access_none;

  CFileStream* fs = openFile(filename);
  if (!fs) {
    response.code = HTTP_STATUS_NOT_FOUND;
    return false;
  }

  file_meta_t meta;
  fs->getMeta(meta);

  // System files start with '.' and are always protected to admin level
  if (filename[0] == '.') {
    meta.readAccess = access_admin;
    meta.writeAccess = access_admin;
  }

  if (access < meta.readAccess) {
    delete fs;
    response.code = HTTP_STATUS_FORBIDDEN;
    return false;
  }

  if (meta.compressed)
    response.headers[hhfn_ContentEncoding] = F("gzip");

  String mime = ContentType::fromFullFileName(filename);

  debug_i("MIME for '%s' is '%s'", filename.c_str(), mime.c_str());

  response.code = HTTP_STATUS_OK;
  //  response.setCache(86400, true);
  return response.sendDataStream(fs, mime);
}


bool CWebServer::start()
{
  if (m_server)
    m_server->close();
  else {
    m_server = new HttpServerEx();
    m_server->addPath(F("/ws"), socketManager.createResource());
    m_server->addPath(F("*"), HttpResourceDelegate(&CWebServer::requestComplete, this));
  }

  uint16_t port = networkManager.webServerPort();
  if (!m_server->listen(port)) {
    debug_w("Web server listen failed");
    return false;
  }

  debug_i("Web server started on port %u", port);
  return true;
}


void CWebServer::stop()
{
  if (m_server) {
    m_server->shutdown();
    // Server will delete itself when last client connection is closed.
    m_server = nullptr;
  }
}


String CWebServer::getMethod() const
{
  return METHOD_WEB();
}


void CWebServer::handleMessage(command_connection_t connection, JsonObject& json)
{
  const char* command = json[ATTR_COMMAND()];

  if (COMMAND_INFO() == command) {
    json[ATTR_SOCKETS()] = WebSocketConnection::getActiveWebSockets().count();

    if (m_server) {
      JsonArray& conns = json.createNestedArray(ATTR_CLIENTS());
      for (unsigned i = 0; i < m_server->connections().count(); i++)
      {
        auto conn = m_server->connections()[i];
        conns.add(conn->getRemoteIp().toString());
      }
    }

    return;
  }

  CCommandHandler::handleMessage(connection, json);
}

