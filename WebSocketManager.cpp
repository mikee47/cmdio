/*
 * WebSocketManager.cpp
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 */

#include "status.h"
#include "WebSocketManager.h"

#if DEBUG_BUILD
//#define DEBUG_WEBSOCKETS
#endif

// Messages
DEFINE_FSTR(ATTR_METHODS, "methods")

WebSocketManager socketManager;

// Roughly 3.5KB RAM per websocket...
#define MAX_WEBSOCKET_COUNT 5

class CCWebSocketResource: public WebsocketResource
{
protected:
	// virtual
	WebSocketConnection* createConnection(HttpServerConnection& connection)
	{
		return new WSCommandConnection(connection);
	}

#ifdef DEBUG_WEBSOCKETS
public:
	CCWebSocketResource() : WebsocketResource()
	{
		onHeadersComplete = HttpResourceDelegate(&CCWebSocketResource::checkHeaders, this);
		onUpgrade = HttpServerConnectionUpgradeDelegate(&CCWebSocketResource::processData, this);
	}

	int checkHeaders(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response)
	{
		int ret = WebsocketResource::checkHeaders(connection, request, response);
		debug_i("checkHeaders() returned %d", ret);
		return ret;
	}

	int processData(HttpServerConnection& connection, HttpRequest& request, char *at, int size)
	{
		int ret = WebsocketResource::processData(connection, request, at, size);
		debug_i("processData() returned %d", ret);
		return ret;
	}
#endif
};

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

command_connection_t WebSocketManager::findConnection(uint32_t cid)
{
	WebSocketsList& list = WSCommandConnection::getActiveWebSockets();
	for (unsigned i = 0; i < list.count(); ++i) {
		auto cc = WSCommandConnection::fromSocket(list[i]);
		if (cc->cid() == cid)
			return cc;
	}
	return nullptr;
}

command_connection_t WebSocketManager::findConnection(const char* cidStr)
{
	return cidStr ? findConnection(strtoul(cidStr, nullptr, 16)) : nullptr;
}

/*
 * After a successful login, the authenticator calls this method.
 */
void WebSocketManager::loginComplete(command_connection_t connection, JsonObject& json)
{
	/*
	 * If the client provided a CID it will identify an previous socket instance.
	 * It may not exist (if it's old) but if so we close it now to preserve resources.
	 */
	auto cc = findConnection(json[ATTR_CID].asString());
	if (cc && cc != connection)
		delete cc;

	// By return we provide this connection's CID, which the client will store as a cookie
	json[ATTR_CID] = String(connection->cid(), 16);

	/*
	 * Client gets a list of authorised methods.
	 */
	JsonArray& methods = json.createNestedArray(ATTR_METHODS);
	for (unsigned i = 0; i < m_handlers.count(); ++i) {
		ICommandHandler* handler = m_handlers[i];
		if (connection->access() >= handler->minAccess())
			methods.add(handler->getMethod());
	}
}

ICommandHandler* WebSocketManager::findHandler(const char* method)
{
	for (unsigned i = 0; i < m_handlers.count(); ++i) {
		ICommandHandler* handler = m_handlers[i];
		if (handler->getMethod() == method)
			return handler;
	}

	return nullptr;
}

void WebSocketManager::handleMessage(command_connection_t connection, JsonObject& json)
{
	const char* method = json[ATTR_METHOD];

	ICommandHandler* handler = findHandler(method);
	if (!handler) {
		setError(json);
		debug_w("Unknown method: '%s'", method);
	}
	else if (connection->access() < handler->minAccess())
		setError(json, ioe_access_denied);
	else
		handler->handleMessage(connection, json);

	if (!json.containsKey(DONT_RESPOND))
		connection->send(json);
}

void WebSocketManager::connected(WebSocketConnection& socket)
{
	auto cc = WSCommandConnection::fromSocket(&socket);
	debug_i("Connected to %s", cc->remoteName().c_str());

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
	WebSocketsList& list = socket.getActiveWebSockets();
	while (list.count() >= MAX_WEBSOCKET_COUNT)
		list[0]->close();
}

void WebSocketManager::disconnected(WebSocketConnection& socket)
{
	auto cc = WSCommandConnection::fromSocket(&socket);
	debug_i("Disconnected from %s", cc->remoteName().c_str());
}

/*
 * Text messages are used for I/O control, status reporting and system methods.
 */
void WebSocketManager::messageReceived(WebSocketConnection& socket, const String& message)
{
	const char* MSG_PING = "?";
	const char* MSG_PONG = "#";
	if (message == MSG_PING) {
		debug_i("Ping!");
		socket.send(MSG_PONG, 1);
		return;
	}

	auto cc = WSCommandConnection::fromSocket(&socket);

	debug_i("Message received from %s: %s", cc->remoteName().c_str(), message.c_str());

	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.parseObject(message);
	if (!json.success()) {
		debug_e("Not a JSON message");
		return;
	}

	handleMessage(cc, json);
}

/*
 * Binary messages are used for OTA firmware updating.
 *
 * Client messages are split into frames by websocket protocol. The WebSocketConnection
 * class does not aggregate frames so we get them individually and in sequence (by TCP).
 *
 */
void WebSocketManager::binaryReceived(WebSocketConnection& socket, uint8_t* data, size_t size)
{
	auto cc = WSCommandConnection::fromSocket(&socket);

	for (unsigned i = 0; i < m_handlers.count(); ++i)
		if (m_handlers[i]->handleData(cc, data, size))
			return;

	debug_w("binaryReceived(%u) - unhandled", size);
}

WebsocketResource* WebSocketManager::createResource()
{
	// Web Sockets configuration
	auto wsResource = new CCWebSocketResource();
	wsResource->setConnectionHandler(connected);
	wsResource->setDisconnectionHandler(disconnected);
	wsResource->setMessageHandler(WebSocketMessageDelegate(&WebSocketManager::messageReceived, this));
	wsResource->setBinaryHandler(WebSocketBinaryDelegate(&WebSocketManager::binaryReceived, this));
	return wsResource;
}

