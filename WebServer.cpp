/*
 * WebServer.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#include "status.h"
#include "Data/Stream/TemplateFileStream.h"

#include "WebServer.h"

#include "FileManager.h"
#include "NetworkManager.h"
#include "WebSocketManager.h"

DEFINE_FSTR(FILE_INDEX_HTML, "index.html")
static DEFINE_FSTR(FILE_CONFIG_HTML, "config.html")
static DEFINE_FSTR(FILE_ERROR_HTML, "error.html")

static DEFINE_FSTR(METHOD_WEB, "web")
static DEFINE_FSTR(ATTR_PATH, "path")
static DEFINE_FSTR(ATTR_CLIENTS, "clients")
static DEFINE_FSTR(ATTR_SOCKETS, "sockets")

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
int WebServer::requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response)
{
	String file = request.uri.relativePath();
#if DEBUG_BUILD
	IPAddress ip = connection.getRemoteIp();
	uint16_t port = connection.getRemotePort();
	debug_i("%s(%s[%u], '%s') from %s:%u", __FUNCTION__, request.methodStr().c_str(), request.method, file.c_str(), ip.toString().c_str(), port);
#endif

	if (file.length() == 0 || (WifiAccessPoint.isEnabled() && !fileExist(file)))
		file = FILE_INDEX_HTML;

//  WifiAccessPoint.isEnabled() ? FILE_CONFIG_HTML() : FILE_INDEX_HTML();

	sendFile(file, request.getQueryParameter(ATTR_CID), response);

	// For errors construct and send error page
	if (response.code >= 400) {
		debug_i("requestComplete(): code = %d", response.code);

//		response.sendFile(FILE_ERROR_HTML(), false);

/*
20/8/2018 - something screwy going on here, still trying to get to the bottom of it
fails in TemplateStream::readMemoryBlock on first read after "continue to plain text" (WAIT mode)

23/8/2018 - I think the problem is somewhere in HTTP because we get a hang when a file
can't be sent in a single TcpConnection::write operation. Perhaps the _stream is getting
destroyed early?
*/

		http_status status = static_cast<http_status>(response.code);

		auto tmpl = new TemplateFileStream(FILE_ERROR_HTML);
		auto &vars = tmpl->variables();
		vars[ATTR_PATH] = request.uri.path();
		vars[ATTR_CODE] = status;
		vars[ATTR_TEXT] = httpGetStatusText(status);
		response.sendTemplate(tmpl);
	}

	return 0;
}

void WebServer::sendFile(const String& filename, const String& cid, HttpResponse& response)
{
	auto cc = socketManager.findConnection(cid.c_str());
	UserRole access = cc ? cc->access() : UserRole::None;

	FileStat stat;
	if (fileStats(filename, &stat) < 0) {
		response.code = HTTP_STATUS_NOT_FOUND;
		return;
	}

	// System files start with '.' and are always protected to admin level
	if (filename[0] == '.') {
		stat.acl.readAccess = UserRole::Admin;
		stat.acl.writeAccess = UserRole::Admin;
	}

	if (access < stat.acl.readAccess)
		response.code = HTTP_STATUS_FORBIDDEN;
	else {
		stat.name = NameBuffer((char*)filename.c_str(), filename.length(), filename.length());
		response.sendFile(stat);
/*
		auto stream = new MemoryDataStream;
		stream->setSize(stat.size);
		auto file = fileOpen(stat);
		if (file < 0) {
			response.code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
			delete stream;
			return;
		}
		// yeay, hacky :-)
		fileRead(file, (char*)stream->getStreamPointer(), stat.size);
		fileClose(file);

		if (stat.attr & eFA_Compressed)
			response.headers[hhfn_ContentEncoding] = F("gzip");

		response.sendDataStream(stream, ContentType::fromFileName(stat.name));
*/
	}
}

bool WebServer::start()
{
	if (m_server)
		m_server->close();
	else {
		HttpServerSettings settings;
		settings.maxActiveConnections = 10;
		settings.keepAliveSeconds = 5;
		settings.minHeapSize = -1;
		settings.useDefaultBodyParsers = true;
		m_server = new HttpServer(settings);
		m_server->addPath(F("/ws"), socketManager.createResource());
		m_server->addPath(F("*"), HttpResourceDelegate(&WebServer::requestComplete, this));
	}

	uint16_t port = networkManager.webServerPort();
	if (!m_server->listen(port)) {
		debug_w("Web server listen failed");
		return false;
	}

	debug_i("Web server started on port %u", port);
	return true;
}

void WebServer::stop()
{
	if (m_server) {
		m_server->shutdown();
		// Server will delete itself when last client connection is closed.
		m_server = nullptr;
	}
}

String WebServer::getMethod() const
{
	return METHOD_WEB;
}

void WebServer::handleMessage(command_connection_t connection, JsonObject& json)
{
	const char* command = json[ATTR_COMMAND];

	if (COMMAND_INFO == command) {
		json[ATTR_SOCKETS] = WebSocketConnection::getActiveWebSockets().count();

		if (m_server) {
			JsonArray& conns = json.createNestedArray(ATTR_CLIENTS);
			for (unsigned i = 0; i < m_server->connections().count(); i++) {
				auto conn = m_server->connections()[i];
				conns.add(conn->getRemoteIp().toString());
			}
		}

		return;
	}

	ICommandHandler::handleMessage(connection, json);
}

