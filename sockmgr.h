/*
 * methods.h
 *
 *  Created on: 5 Jun 2018
 *      Author: Mike
 *
 * Websocket commands are grouped into methods, each of which has a minimum access
 * level. To simplify addition of methods and correct access control we manage this
 * centrally so 'plugin' modules can register themselves.
 *
 */

#ifndef __METHODS_H
#define __METHODS_H

#include <SmingCore/SmingCore.h>
#include <SmingCore/Network/Http/Websocket/WebsocketResource.h>
//#include <network.h>

#include "cmdhandler.h"


class CSocketManager
{
  private:
    Vector<CCommandHandler*> m_handlers;
    CCommandHandler* findHandler(const char* method);

  private:
    // WebSocketResource callbacks
    static void connected(WebSocketConnection& socket);
    static void disconnected(WebSocketConnection& socket);
    void messageReceived(WebSocketConnection& socket, const String& message);
    void binaryReceived(WebSocketConnection& socket, uint8_t* data, size_t size);

    void handleAuthMessage(command_connection_t connection, JsonObject& json);
    void handleMessage(command_connection_t connection, JsonObject& json);

  public:
    WebsocketResource* createResource();

    void registerHandler(CCommandHandler& handler)
    {
      if (!m_handlers.contains(&handler))
        m_handlers.add(&handler);
    }

    void unregisterHandler(CCommandHandler& handler)
    {
      m_handlers.removeElement(&handler);
    }

    void loginComplete(command_connection_t connection, JsonObject& json);
};


extern CSocketManager socketManager;


#endif // __METHODS_H


