/*
 * socket_client.h
 *
 *  Created on: 5 Jun 2018
 *      Author: Mike
 */

#ifndef __SOCKHANDLER_H
#define __SOCKHANDLER_H

#include <SmingCore/SmingCore.h>
#include <SmingCore/Network/Http/Websocket/WebsocketResource.h>
#include <cmdio/status.h>
#include <cmdio/access.h>

// Tag used in messages to identify method; responses must contain the same method
DECLARE_STRING_P(ATTR_METHOD)
DECLARE_STRING_P(ATTR_COMMAND)
DECLARE_STRING_P(COMMAND_INFO)
DECLARE_STRING_P(ATTR_NAME)

/*
 * Websockets stay open indefinitely, but we enforce a couple of timeouts.
 *
 * Immediately after opening, setAccess() should be called; a short timeout
 * is applied here.
 *
 * The client application will use the socket at least every 30 seconds, sending
 * a 'ping' if required. We use a longer timeout to defend against stalled applications.
 *
 * CSocketManager limits the number of open websockets, and these timeouts reduce
 * the chance of an active websocket being closed.
 *
 * Timeouts are in seconds
 */
#define WS_INITIAL_TIMEOUT  3
#define WS_ACTIVE_TIMEOUT   60



/*
 * We use a websockets userdata property to store additional flags.
 *
 * Primarily we need this to indicate whether a socket has been
 * authenticated or not. i.e. what level of access is permitted.
 * At present we have guest (unauthenticated) and admin.
 * This is mainly to prevent inadvertent configuration changes.
 * Firmware updates are further protected by strong encryption.
 *
 * We only use admin functions over a secure LAN or HTTPS, so we
 * take no measures to prevent MIM or other attacks. Neither are
 * elaborate challenge-response protocols necessary.
 *
 * Authenticating general HTTP file access is more awkward because
 * it's stateless; we'd need the client to store a token to deal
 * with this.
 *
 */
struct __attribute__((packed)) cc_data_t {
  // Socket has been authenticated for admin access
  access_type_t access;
  // Command handler can prevent automatic response by clearing this flag
  bool fSendResponse: 1;
};



/*
 * We use a typedef to simplify implementing over a different type of connection.
 * Commands may be invoked without a connection so we must allow for connection = nullptr.
 */
class CWSCommandConnection;
typedef CWSCommandConnection* command_connection_t;


/*
 * We override WebsocketResource to create instances of this class, so
 * we can customise functionality, add member data, etc.
 */
class CWSCommandConnection: public WebSocketConnection
{
  private:
    // Uniquely identifies this connection
    uint32_t m_cid;

  private:
    cc_data_t& wsData()
    {
      assert(sizeof(cc_data_t) <= sizeof(userData));
      return *reinterpret_cast<cc_data_t*>(&userData);
    }

  public:
    static command_connection_t fromSocket(WebSocketConnection* socket)
    {
      return static_cast<command_connection_t>(socket);
    }

    CWSCommandConnection(HttpServerConnection* conn);

    using WebSocketConnection::send;

    bool isValid()
    {
      return this ? getActiveWebSockets().contains(this) : false;
    }

    void send(const String& msg);
    void send(JsonObject& json);
    static void broadcast(const String& msg);
    static void broadcast(JsonObject& json);

    // Permitted access type
    access_type_t access()
    {
      return wsData().access;
    }

    void setAccess(access_type_t access)
    {
      wsData().access = access;
      getHttpConnection().setTimeOut(WS_ACTIVE_TIMEOUT);
    }

    // Command handler calls this if it intends to respond later
    void doNotRespond()
    {
      wsData().fSendResponse = false;
    }

    bool respond()
    {
      return wsData().fSendResponse;
    }

    // About to invoke command handler, set defaults
    void reset()
    {
      wsData().fSendResponse = true;
    }

    String remoteName()
    {
      IPAddress ip = getHttpConnection().getRemoteIp();
      uint16_t port = getHttpConnection().getRemotePort();
      return ip.toString() + ":" + String(port);
    }

    uint32_t cid()
    {
      return m_cid;
    }
};




// Handler classes
class CCommandHandler
{
  public:
    virtual ~CCommandHandler()
    {}

    virtual String getMethod() const = 0;


    /*
     * Get access type required for this command.
     *
     * @param command Specify NULL to get minimum access for this handler.
     */
    virtual access_type_t minAccess() const
    {
      // By default, require maximum access.
      return access_admin;
    }

    virtual void handleMessage(command_connection_t connection, JsonObject& json)
    {
      setError(json, ioe_bad_command);
    }

    // Return true if data consumed
    virtual bool handleData(command_connection_t connection, uint8_t* data, size_t size)
    {
      return false;
    }

    bool operator ==(const CCommandHandler& handler) const
    {
      return (this == &handler);
    }


};



#endif // __SOCKHANDLER_H
