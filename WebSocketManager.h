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

#ifndef __WEBSOCKET_MANAGER_H
#define __WEBSOCKET_MANAGER_H

#include "WString.h"
#include "Network/Http/Websocket/WebsocketResource.h"
#include "CommandHandler.h"

DECLARE_FSTR(ATTR_CID)

class WebsocketManager
{
public:
	WebsocketResource* createResource();

	void registerHandler(ICommandHandler& handler)
	{
		if (!m_handlers.contains(&handler))
			m_handlers.add(&handler);
	}

	void unregisterHandler(ICommandHandler& handler)
	{
		m_handlers.removeElement(&handler);
	}

	void loginComplete(command_connection_t connection, JsonObject& json);

	static command_connection_t findConnection(uint32_t cid);
	static command_connection_t findConnection(const char* cidStr);

private:
	// WebsocketResource callbacks
	static void connected(WebsocketConnection& socket);
	static void disconnected(WebsocketConnection& socket);
	void messageReceived(WebsocketConnection& socket, const String& message);
	void binaryReceived(WebsocketConnection& socket, uint8_t* data, size_t size);

	void handleAuthMessage(command_connection_t connection, JsonObject& json);
	void handleMessage(command_connection_t connection, JsonObject& json);

private:
	Vector<ICommandHandler*> m_handlers;
	ICommandHandler* findHandler(const char* method);
};

extern WebsocketManager socketManager;

#endif // __WEBSOCKET_MANAGER_H

