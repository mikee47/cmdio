/*
 * CommandConnection.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#include "CommandConnection.h"

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

WSCommandConnection::WSCommandConnection(WebsocketConnection& socket) : socket(socket)
{
	auto cc = fromSocket(&socket);
	if(cc != nullptr) {
		debug_e("ERROR! WSCommandConnection() - cc = %p", cc);
		delete cc;
	}
	socket.setUserData(this);
	cid = createCID(this);

	setTimeout(WS_INITIAL_TIMEOUT);
}

/*
 * Send message via WebSocket, safely
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

	if(active()) {
		socket.send(msg);
	}
}

void WSCommandConnection::send(JsonObjectConst json)
{
	String s;
	Json::serialize(json, s);
	send(s);
}
