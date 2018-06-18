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


HttpServerEx* m_server;

static DEFINE_STRING_P(FILE_INDEX_HTML, "index.html")
static DEFINE_STRING_P(FILE_CONFIG_HTML, "config.html")
static DEFINE_STRING_P(FILE_ERROR_HTML, "error.html")

static DEFINE_STRING_P(METHOD_WEB, "web")
static DEFINE_STRING_P(ATTR_PATH, "path")
static DEFINE_STRING_P(ATTR_CLIENTS, "clients")
static DEFINE_STRING_P(ATTR_SOCKETS, "sockets")


/*
 * All web file requests come here.
 */
int CWebServer::requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response)
{
  String file = request.getPath();
  file.toLowerCase();
  IPAddress ip = connection.getRemoteIp();
  uint16_t port = connection.getRemotePort();
  debug_i("%s(%s[%u], '%s') from %s:%u", __FUNCTION__, http_method_str(request.method), request.method, file.c_str(), ip.toString().c_str(), port);

  if (request.method != HTTP_GET && request.method != HTTP_POST && request.method != HTTP_HEAD)
    response.code = HTTP_STATUS_NOT_IMPLEMENTED;
  else {
    if (file[0] == '/')
      file = file.substring(1);

    // Protect system files
    if (file[0] == '.') {
      response.code = HTTP_STATUS_FORBIDDEN;
    }
    else {
      if (file.length() == 0 || (WifiAccessPoint.isEnabled() && !fileExist(file)))
        file = FILE_INDEX_HTML();

          //WifiAccessPoint.isEnabled() ? FILE_CONFIG_HTML() : FILE_INDEX_HTML();

    //  response.setCache(86400, true);
      response.sendFile(file);
    }
  }

  // For errors construct and send error page
  if (response.code >= 400) {
    http_status status = static_cast<http_status>(response.code);
    const char* text = HttpServerConnection::getStatus(status);

    TemplateFileStream *tmpl = new TemplateFileStream(FILE_ERROR_HTML());
    auto &vars = tmpl->variables();
    vars[ATTR_PATH()] = request.getPath();
    vars[ATTR_CODE()] = status;
    vars[ATTR_TEXT()] = text;
    response.sendTemplate(tmpl);
  }

  return 0;
}


bool CWebServer::start()
{
  if (m_server)
    m_server->close();
  else {
    m_server = new HttpServerEx();
    m_server->addPath("/ws", socketManager.createResource());
    m_server->addPath("*", HttpResourceDelegate(&CWebServer::requestComplete, this));
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
      for (int i = 0; i < m_server->connections().count(); i++)
      {
        auto conn = m_server->connections()[i];
        conns.add(conn->getRemoteIp().toString());
      }
    }

    return;
  }

  CCommandHandler::handleMessage(connection, json);
}

