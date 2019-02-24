/*
 * CommandHandler.h
 *
 *  Created on: 5 Jun 2018
 *      Author: mikee47
 */

#ifndef __CMDHANDLER_H
#define __CMDHANDLER_H

#include <Network/Http/Websocket/WebsocketResource.h>
#include <status.h>
#include "CommandConnection.h"

// Tag used in messages to identify method; responses must contain the same method
DECLARE_FSTR(ATTR_METHOD)
DECLARE_FSTR(ATTR_COMMAND)
DECLARE_FSTR(COMMAND_INFO)
DECLARE_FSTR(ATTR_NAME)
DECLARE_FSTR(DONT_RESPOND)

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
 * We override WebsocketResource to create instances of this class, so
 * we can customise functionality, add member data, etc.
 */
class WSCommandConnection: public WebsocketConnection, public ICommandConnection
{
public:
	static WSCommandConnection* fromSocket(WebsocketConnection* socket)
	{
		return reinterpret_cast<WSCommandConnection*>(socket);
	}

	WSCommandConnection(HttpServerConnection& conn);

	using WebsocketConnection::send;

	bool active()
	{
		return getActiveWebsockets().contains(this);
	}

	void send(const String& msg);
	void send(JsonObject& json);
	static void broadcast(const String& msg);
	static void broadcast(JsonObject& json);

	// Permitted access type
	UserRole access()
	{
		return m_access;
	}

	void setAccess(UserRole access)
	{
		m_access = access;
		getHttpConnection().setTimeOut(WS_ACTIVE_TIMEOUT);
	}

	IPAddress getRemoteIp()
	{
		return getHttpConnection().getRemoteIp();
	}

	String remoteName()
	{
		return getRemoteIp().toString() + ":" + String(getHttpConnection().getRemotePort());
	}

	uint32_t cid()
	{
		return m_cid;
	}

private:
	// Uniquely identifies this connection
	uint32_t m_cid;

	/*
	 * Indicates level of access permitted by this connection.
	 * We need this to prevent inadvertent configuration changes.
	 * Firmware updates are further protected by strong encryption.
	 *
	 * We only use admin functions over a secure LAN or HTTPS, so we
	 * take no measures to prevent MIM or other attacks. Neither are
	 * elaborate challenge-response protocols necessary.
	 *
	 * Authenticating general HTTP file access is more awkward because
	 * it's stateless; we'd need the client to store a token to deal
	 * with this.
	 */
	UserRole m_access;
};

// Handler classes
class ICommandHandler
{
public:
	virtual ~ICommandHandler()
	{
	}

	virtual String getMethod() const = 0;

	/*
	 * Get access type required for this command.
	 *
	 * @param command Specify NULL to get minimum access for this handler.
	 */
	virtual UserRole minAccess() const
	{
		// By default, require maximum access.
		return UserRole::Admin;
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

	bool operator ==(const ICommandHandler& handler) const
						{
		return (this == &handler);
	}

};

#endif // __CMDHANDLER_H
