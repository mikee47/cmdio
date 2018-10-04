/*
 * CommandHandler.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#include "CommandHandler.h"

DEFINE_FSTR(ATTR_METHOD, "method")
DEFINE_FSTR(ATTR_COMMAND, "command")
DEFINE_FSTR(COMMAND_INFO, "info")
DEFINE_FSTR(ATTR_NAME, "name")
DEFINE_FSTR(DONT_RESPOND, "DR")

/*
 * We a socket identifier that is unique and doesn't get re-used even after reboots.
 * This is used by the socket manager to help avoid multiple client instances.
 *
 */
static uint32_t createCID(void* instance)
{
	return os_random();
//  return RTC.getRtcSeconds() ^ uint32_t(instance);
}

WSCommandConnection::WSCommandConnection(HttpServerConnection& conn) :
	WebSocketConnection(conn)
{
	conn.setTimeOut(WS_INITIAL_TIMEOUT);
	m_cid = createCID(this);
}

/*
 * Send message via WebSocket, safely
 *
 * @param socket Specify nullptr to broadcast.
 * @param msg Message to send
 */
void WSCommandConnection::send(const String& msg)
{
	debug_i("%s %u bytes: %s", __FUNCTION__, msg.length(), msg.c_str());

	if (active())
		WebSocketConnection::sendString(msg);
}

void WSCommandConnection::send(JsonObject& json)
{
	String s;
	json.printTo(s);
	send(s);
}

void WSCommandConnection::broadcast(const String& msg)
{
	debug_i("%s(%s)", __FUNCTION__, msg.c_str());
	WebSocketConnection::broadcast(msg.c_str(), msg.length());
}

void WSCommandConnection::broadcast(JsonObject& json)
{
	String s;
	json.printTo(s);
	broadcast(s);
}

