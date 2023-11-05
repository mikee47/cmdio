/*
 * WebsocketManager.cpp
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 */

#include "WebsocketManager.h"
#include "AuthManager.h"
#include <Data/Stream/SharedMemoryStream.h>

#if DEBUG_BUILD
//#define DEBUG_WEBSOCKETS
#endif

// Messages
DEFINE_FSTR(ATTR_METHODS, "methods")

WebsocketManager socketManager;

// Roughly 3.5KB RAM per websocket...
#define MAX_WEBSOCKET_COUNT 5

/*
 * We only want a single websocket per client. HTTP cannot be used to uniquely identify
 * a client so we tag every outgoing command with a CID (Connection ID). The client
 * script stores this in a cookie and includes it in every incoming command.
 *
 * When the client opens a websocket connection, if the CID corresponds with another
 * websocket then that websocket is closed. If a client has multiple browser instances
 * open then only one of them will be active - it doesn't matter which one.
 *
 * We need to create the CID carefully to minimise the chance of an 'old' CID being
 * reused. Fortunately, there's a proper random number generator available.
 *
 */
DEFINE_FSTR(ATTR_CID, "cid")

bool WebsocketManager::isValidConnection(const WSCommandConnection* cc)
{
	for(auto skt : WebsocketConnection::getActiveWebsockets()) {
		auto con = WSCommandConnection::fromSocket(skt);
		if(con == cc) {
			return true;
		}
	}
	return false;
}

WSCommandConnection* WebsocketManager::findConnection(uint32_t cid)
{
	for(auto skt : WebsocketConnection::getActiveWebsockets()) {
		auto con = WSCommandConnection::fromSocket(skt);
		if(con && con->getCid() == cid) {
			return con;
		}
	}
	return nullptr;
}

WSCommandConnection* WebsocketManager::findConnection(const char* cidStr)
{
	return cidStr ? findConnection(strtoul(cidStr, nullptr, 16)) : nullptr;
}

/*
 * After a successful login, the authenticator calls this method.
 */
void WebsocketManager::loginComplete(WSCommandConnection* connection, JsonObject json)
{
	/*
	 * If the client provided a CID it will identify an previous socket instance.
	 * It may not exist (if it's old) but if so we close it now to preserve resources.
	 */
	auto cc = findConnection(json[ATTR_CID].as<const char*>());
	if(cc != nullptr && cc != connection) {
		debug_i("WebsocketManager: deleting old connection");
		delete cc;
	}

	// By return we provide this connection's CID, which the client will store locally
	json[ATTR_CID] = String(connection->getCid(), 16);

	/*
	 * Client gets a list of authorised methods.
	 */
	auto methods = json.createNestedObject(ATTR_METHODS);
	for(auto handler : handlers) {
		auto info = handler->getPageInfo();
		if(connection->getAccess() < info.acl.readAccess) {
			continue;
		}
		auto method = methods.createNestedObject(handler->getMethod());
		method[ATTR_NAME] = info.name;
		if(connection->getAccess() < info.acl.writeAccess) {
			method["ro"] = true;
		}
	}
}

WSCommandHandler* WebsocketManager::findHandler(const char* method)
{
	for(auto handler : handlers) {
		if(handler->getMethod() == method) {
			return handler;
		}
	}

	return nullptr;
}

void WebsocketManager::broadcast(const void* data, size_t length, ws_frame_type_t type)
{
	debug_i("WSCommandConnection::broadcast(%u bytes)", length);

	char* copy = new char[length];
	memcpy(copy, data, length);
	std::shared_ptr<const char> sharedData(copy, [](const char* ptr) { delete[] ptr; });

	for(auto skt : WebsocketConnection::getActiveWebsockets()) {
		auto cc = WSCommandConnection::fromSocket(skt);
		if(cc && cc->getAccess() >= UserRole::User) {
			skt->send(new SharedMemoryStream<const char>(sharedData, length), type);
		}
	}
}

void WebsocketManager::broadcast(const String& msg)
{
	debug_i("WSCommandConnection::broadcast(\"%s\")", msg.c_str());
	broadcast(msg.c_str(), msg.length(), WS_FRAME_TEXT);
}

void WebsocketManager::broadcast(JsonObjectConst json)
{
	broadcast(Json::serialize(json));
}

void WebsocketManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* method = json[ATTR_METHOD];

	WSCommandHandler* handler = findHandler(method);
	if(!handler) {
		IO::setError(json, IO::Error::bad_command);
		debug_w("Unknown method: '%s'", method);
	} else if(connection->getAccess() < handler->getAccess().readAccess) {
		IO::setError(json, IO::Error::access_denied);
	} else {
		handler->handleMessage(connection, json);
	}

	if(!json.containsKey(DONT_RESPOND)) {
		connection->send(json);
	}
}

void WebsocketManager::connected(WebsocketConnection& socket)
{
	auto cc = new WSCommandConnection(socket);
	(void)cc;

	debug_i("Connected to %s", cc->getRemoteName().c_str());

	/*
	 * Only permit max. 1 socket per client to preserve resources.
	 *
	 * Connecting through a proxy we get the proxy address rather
	 * than the client, so we can't use the IP address to limit this.
	 * Instead we just limit the number of active sockets, closing the
	 * oldest one if a new connection arrives.
	 *
	 * TODO: If the socket isn't authenticated within, say, 5 seconds,
	 * then close it. Can we do that using TCP timeouts?
	 */
	auto& list = WebsocketConnection::getActiveWebsockets();
	while(list.count() >= MAX_WEBSOCKET_COUNT) {
		list[0]->close();
	}
}

void WebsocketManager::disconnected(WebsocketConnection& socket)
{
	auto cc = WSCommandConnection::fromSocket(&socket);
	if(cc == nullptr) {
		return;
	}
	debug_i("Disconnected from %s", cc == nullptr ? "(null)" : cc->getRemoteName().c_str());
	authManager.disconnected(*cc);

	delete cc;
}

/*
 * Text messages are used for I/O control, status reporting and system methods.
 */
void WebsocketManager::messageReceived(WebsocketConnection& socket, const String& message)
{
	const char* MSG_PING = "?";
	const char* MSG_PONG = "#";
	if(message == MSG_PING) {
		debug_i("Ping!");
		socket.send(MSG_PONG, 1);
		return;
	}

	auto cc = WSCommandConnection::fromSocket(&socket);
	if(cc == nullptr) {
		return;
	}

	debug_i("Message received from %s: %s", cc->getRemoteName().c_str(), message.c_str());

	DynamicJsonDocument doc(1024);
	auto err = deserializeJson(doc, message);
	if(err) {
		debug_e("Not a JSON message");
		return;
	}

	handleMessage(cc, doc.as<JsonObject>());
}

/*
 * Binary messages are used for OTA firmware updating.
 *
 * Client messages are split into frames by websocket protocol. The WebsocketConnection
 * class does not aggregate frames so we get them individually and in sequence (by TCP).
 *
 */
void WebsocketManager::binaryReceived(WebsocketConnection& socket, uint8_t* data, size_t size)
{
	auto cc = WSCommandConnection::fromSocket(&socket);
	if(cc == nullptr) {
		return;
	}

	for(unsigned i = 0; i < handlers.count(); ++i) {
		if(handlers[i]->handleData(cc, data, size)) {
			return;
		}
	}

	debug_w("binaryReceived(%u) - unhandled", size);
}

WebsocketResource* WebsocketManager::createResource()
{
	// Web Sockets configuration
	auto wsResource = new WebsocketResource();
	wsResource->setConnectionHandler(connected);
	wsResource->setDisconnectionHandler(disconnected);
	wsResource->setMessageHandler(WebsocketMessageDelegate(&WebsocketManager::messageReceived, this));
	wsResource->setBinaryHandler(WebsocketBinaryDelegate(&WebsocketManager::binaryReceived, this));
	return wsResource;
}

void WebsocketManager::fileChange(const String& filename)
{
	for(auto& handler : handlers) {
		handler->fileChange(filename);
	}
}
