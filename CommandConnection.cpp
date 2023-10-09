/*
 * CommandConnection.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#include "CommandConnection.h"
#include "WebsocketManager.h"

namespace
{
/*
 * We a socket identifier that is unique and doesn't get re-used even after reboots.
 * This is used by the socket manager to help avoid multiple client instances.
 *
 */
uint32_t createCID(void* instance)
{
	(void)instance;
	return os_random();
}

} // namespace

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

bool WSCommandConnection::isActive(const WSCommandConnection* cc)
{
	return socketManager.isValidConnection(cc);
}
