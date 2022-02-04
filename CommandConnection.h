/*
 * CommandConnection.h
 *
 *  Created on: 27 Jun 2018
 *      Author: mikee47
 *
 * Provides an abstraction for handling user commands, typically via websocket, but allows
 * use of other transports.
 *
 * Commands and responses use JSON.
 *
 * The command group specifies a minimum access level.
 */

#pragma once

#include <ArduinoJson.h>
#include <IpAddress.h>
#include <IFS/Access.h>
#include <Network/Http/Websocket/WebsocketConnection.h>

using UserRole = IFS::UserRole;

/*
 * Websockets stay open indefinitely, but we enforce a couple of timeouts.
 *
 * Immediately after connection, setAccess() should be called to validate the user.
 * A short timeout is applied here so the socket is not left hanging open.
 *
 * The client application will use the socket at least every 30 seconds, sending
 * a 'ping' if required. We use a longer timeout to defend against stalled applications.
 *
 * WebsocketManager limits the number of open websockets, and these timeouts reduce
 * the chance of an active websocket being closed.
 *
 * Timeouts are in seconds.
 */
#define WS_INITIAL_TIMEOUT 3 ///< Max. time after initial connection to validate
#define WS_ACTIVE_TIMEOUT 60 ///< Need activity on socket to reset this timeout

/*
 * Base class for interacting either with user via websocket connection
 */
class WSCommandConnection
{
public:
	static WSCommandConnection* fromSocket(WebsocketConnection* socket)
	{
		if(socket == nullptr) {
			return nullptr;
		} else {
			return static_cast<WSCommandConnection*>(socket->getUserData());
		}
	}

	WSCommandConnection(WebsocketConnection& socket);

	~WSCommandConnection()
	{
		socket.setUserData(nullptr);
	}

	// Check this connection is still active
	bool active()
	{
		return socket.getActiveWebsockets().contains(&socket);
	}

	void send(const String& msg);
	void send(JsonObjectConst json);

	// Permitted access type
	UserRole getAccess()
	{
		return access;
	}

	void setAccess(UserRole access)
	{
		this->access = access;
		setTimeout(WS_ACTIVE_TIMEOUT);
	}

	void setTimeout(unsigned timeoutSecs)
	{
		auto conn = getHttpConnection();
		if(conn != nullptr) {
			conn->setTimeOut(WS_ACTIVE_TIMEOUT);
		}
	}

	HttpConnection* getHttpConnection()
	{
		return socket.getConnection();
	}

	IpAddress getRemoteIp()
	{
		auto conn = getHttpConnection();
		return conn == nullptr ? IpAddress() : conn->getRemoteIp();
	}

	String getRemoteName()
	{
		auto conn = getHttpConnection();
		if(conn == nullptr) {
			return nullptr;
		} else {
			return conn->getRemoteIp().toString() + ':' + String(conn->getRemotePort());
		}
	}

	uint32_t getCid()
	{
		return cid;
	}

private:
	WebsocketConnection& socket;

	// Uniquely identifies this connection
	uint32_t cid;

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
	UserRole access{};
};
