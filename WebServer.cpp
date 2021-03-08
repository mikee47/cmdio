/*
 * WebServer.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#include <Data/Stream/TemplateFileStream.h>
#include <JsonDirectoryStream.h>
#include <Data/Stream/HtmlDirectoryStream.h>

#include "WebServer.h"

#include "WebsocketManager.h"
#include "FileManager.h"
#include "NetworkManager.h"
#include <Network/WebHelpers/escape.h>

#include <IO/Strings.h>

DEFINE_FSTR(FILE_INDEX_HTML, "index.html");
DEFINE_FSTR_LOCAL(FILE_CONFIG_HTML, "config.html");
DEFINE_FSTR_LOCAL(FILE_ERROR_HTML, "error.html");

DEFINE_FSTR_LOCAL(METHOD_WEB, "web");
DEFINE_FSTR_LOCAL(ATTR_PATH, "path");
DEFINE_FSTR_LOCAL(ATTR_CODE, "code");
DEFINE_FSTR_LOCAL(ATTR_TEXT, "text");
DEFINE_FSTR_LOCAL(ATTR_CLIENTS, "clients");
DEFINE_FSTR_LOCAL(ATTR_SOCKETS, "sockets");

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
	if(requestCompleteDelegate) {
		if(requestCompleteDelegate(connection)) {
			return 0;
		}
	}

	PSTR_ARRAY(funcName, "WebServer::requestComplete");

	String file = request.uri.getRelativePath();
#if DEBUG_BUILD
	IpAddress ip = connection.getRemoteIp();
	uint16_t port = connection.getRemotePort();
	debug_i("%s(%s[%u], '%s') from %s:%u", funcName, request.methodStr().c_str(), request.method, file.c_str(),
			ip.toString().c_str(), port);
	String s = request.uri.toString().c_str();
	debug_hex(INFO, "URI", s.c_str(), s.length());
#endif

	auto contentType = ContentType::fromString(request.headers[HTTP_HEADER_CONTENT_TYPE]);

	if(!contentType) {
		if(file.length() == 0 || (WifiAccessPoint.isEnabled() && !fileExist(file))) {
			file = FILE_INDEX_HTML;
		}
	}

	//  WifiAccessPoint.isEnabled() ? FILE_CONFIG_HTML() : FILE_INDEX_HTML();

	sendFile(file, contentType, request.getQueryParameter(ATTR_CID), response);

	// For errors construct and send error page
	if(response.code >= 400) {
		debug_i("%s(): code = %d", funcName, response.code);

		auto status = http_status(response.code);
		auto tmpl = new TemplateFileStream(FILE_ERROR_HTML);
		auto& vars = tmpl->variables();
		vars[ATTR_PATH] = request.uri.Path;
		vars[ATTR_CODE] = status;
		vars[ATTR_TEXT] = httpGetStatusText(status);
		response.sendNamedStream(tmpl);
	}

	return 0;
}

void WebServer::sendFile(const String& filename, MimeType contentType, const String& cid, HttpResponse& response)
{
	auto cc = socketManager.findConnection(cid.c_str());
	UserRole access = (cc == nullptr) ? UserRole::None : cc->getAccess();

	FileStat stat;
	if(fileStats(filename, stat) < 0) {
		response.code = HTTP_STATUS_NOT_FOUND;
		return;
	}

	// System files start with '.' and are always protected to admin level
	if(filename[0] == '.') {
		stat.acl.readAccess = UserRole::Admin;
		stat.acl.writeAccess = UserRole::Admin;
	}

	if(access < stat.acl.readAccess) {
		debug_w("File \"%s\" requires '%s' access, connection is '%s'", filename.c_str(),
				toString(stat.acl.readAccess).c_str(), toString(access).c_str());
		response.code = HTTP_STATUS_FORBIDDEN;
		return;
	}

	stat.name = IFS::NameBuffer{const_cast<String&>(filename)};
	if(stat.attr[File::Attribute::Directory]) {
		auto dir = new DirectoryStream(filename);
		auto err = dir->getLastError();
		if(err < 0) {
			response.code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
			delete dir;
			return;
		}

		switch(contentType) {
		case MIME_JSON:
			response.sendDataStream(new JsonDirectoryStream(dir), contentType);
			break;
		case MIME_HTML:
			response.sendDataStream(new HtmlDirectoryStream(dir), contentType);
			break;
		default:
			response.code = HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
		}
		return;
	}

	// Regular file
	response.sendFile(stat);
}

bool WebServer::start(const HttpServerSettings& settings)
{
	if(server != nullptr) {
		server->close();
	} else {
		server = new HttpServer(settings);
		server->paths.set(F("/ws"), socketManager.createResource());
		server->paths.setDefault(HttpResourceDelegate(&WebServer::requestComplete, this));
		server->setBodyParser(MIME_JSON, bodyToStringParser);
		server->setBodyParser(MIME_XML, bodyToStringParser);
	}

	uint16_t port = networkManager.webServerPort();
	if(!server->listen(port)) {
		debug_w("Web server listen failed");
		return false;
	}

	debug_i("Web server started on port %u", port);
	return true;
}

void WebServer::stop()
{
	if(server) {
		server->shutdown();
		// Server will delete itself when last client connection is closed.
		server = nullptr;
	}
}

String WebServer::getMethod() const
{
	return METHOD_WEB;
}

void WebServer::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];

	if(COMMAND_INFO == command) {
		json[ATTR_SOCKETS] = socketManager.count();

		if(server) {
			JsonArray conns = json.createNestedArray(ATTR_CLIENTS);

			auto& connections = server->getConnections();
			for(unsigned i = 0; i < connections.count(); i++) {
				auto conn = connections[i];
				conns.add(conn->getRemoteIp().toString());
			}
		}

		return;
	}

	WSCommandHandler::handleMessage(connection, json);
}
