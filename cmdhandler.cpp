/*
 * commands.cpp
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */

#include "cmdhandler.h"


DEFINE_STRING_P(ATTR_METHOD, "method")
DEFINE_STRING_P(ATTR_COMMAND, "command")
DEFINE_STRING_P(COMMAND_INFO, "info")
DEFINE_STRING_P(ATTR_NAME, "name")
DEFINE_STRING_P(DONT_RESPOND, "DR")


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


CWSCommandConnection::CWSCommandConnection(HttpServerConnection* conn) :
  WebSocketConnection(conn)
{
  conn->setTimeOut(WS_INITIAL_TIMEOUT);
  m_cid = createCID(this);
}


/*
 * Send message via WebSocket, safely
 *
 * @param socket Specify nullptr to broadcast.
 * @param msg Message to send
 */
void CWSCommandConnection::send(const String& msg)
{
  debug_i("%s %u bytes: %s", __FUNCTION__, msg.length(), msg.c_str());

  if (active())
    WebSocketConnection::sendString(msg);
}

void CWSCommandConnection::send(JsonObject& json)
{
  String s;
  json.printTo(s);
  send(s);
}

void CWSCommandConnection::broadcast(const String& msg)
{
  debug_i("%s(%s)", __FUNCTION__, msg.c_str());
  WebSocketConnection::broadcast(msg.c_str(), msg.length());
}

void CWSCommandConnection::broadcast(JsonObject& json)
{
  String s;
  json.printTo(s);
  broadcast(s);
}

