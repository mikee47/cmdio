/*
 * WebsocketManager.h
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 *
 * Websocket commands are grouped into methods, each of which has a minimum access
 * level. To simplify addition of methods and correct access control we manage this
 * centrally so 'plugin' modules can register themselves.
 *
 */

#pragma once

#include "WString.h"
#include "Network/Http/Websocket/WebsocketResource.h"
#include "CommandHandler.h"

DECLARE_FSTR(ATTR_CID)

class WebsocketManager
{
public:
	WebsocketResource* createResource();

	void registerHandler(WSCommandHandler& handler)
	{
		if(!handlers.contains(&handler))
			handlers.add(&handler);
	}

	void unregisterHandler(WSCommandHandler& handler)
	{
		handlers.removeElement(&handler);
	}

	void loginComplete(WSCommandConnection* connection, JsonObject json);

	static WSCommandConnection* findConnection(uint32_t cid);
	static WSCommandConnection* findConnection(const char* cidStr);

	static void broadcast(const String& msg);
	static void broadcast(JsonObjectConst json);

	static unsigned count()
	{
		return WebsocketConnection::getActiveWebsockets().count();
	}

	void fileChange(const String& filename);

private:
	// WebSocketResource callbacks
	static void connected(WebsocketConnection& socket);
	static void disconnected(WebsocketConnection& socket);
	void messageReceived(WebsocketConnection& socket, const String& message);
	void binaryReceived(WebsocketConnection& socket, uint8_t* data, size_t size);

	void handleAuthMessage(WSCommandConnection* connection, JsonObject json);
	void handleMessage(WSCommandConnection* connection, JsonObject json);

private:
	Vector<WSCommandHandler*> handlers;
	WSCommandHandler* findHandler(const char* method);
};

extern WebsocketManager socketManager;
