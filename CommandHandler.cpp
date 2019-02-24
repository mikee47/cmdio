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
	WebsocketConnection(conn)
{
	conn.setTimeOut(WS_INITIAL_TIMEOUT);
	m_cid = createCID(this);
}

/*
 * Send message via Websocket, safely
 *
 * @param socket Specify nullptr to broadcast.
 * @param msg Message to send
 */
void WSCommandConnection::send(const String& msg)
{
	debug_i("WSCommandConnection::send(\"%s\"), %u bytes", msg.c_str(), msg.length());

/*
	debug_i("WSCommandConnection::send(%u)", msg.length());
	debug_hex(INFO, "MSG", msg.c_str(), msg.length(), 0);
	for (unsigned i = 0; i < msg.length(); ++i) {
		char c = msg[i];
		if (c < 0x20 || c > 127)
			debug_i("Bad char @ %u", i);
	}
*/

	if (active())
		WebsocketConnection::sendString(msg);
}

void WSCommandConnection::send(JsonObject& json)
{
	String s;
	json.printTo(s);
	send(s);
}

void WSCommandConnection::broadcast(const String& msg)
{
	debug_i("WSCommandConnection::broadcast(\"%s\")", msg.c_str());
	WebsocketConnection::broadcast(msg.c_str(), msg.length());
}

void WSCommandConnection::broadcast(JsonObject& json)
{
	String s;
	json.printTo(s);
	broadcast(s);
}

