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

using ACL = IFS::ACL;
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
		return socket ? static_cast<WSCommandConnection*>(socket->getUserData()) : nullptr;
	}

	WSCommandConnection(WebsocketConnection& socket);

	~WSCommandConnection()
	{
		socket.setUserData(nullptr);
	}

	// Check this connection is still active
	bool __forceinline active() const
	{
		return isActive(this);
	}

	static bool isActive(const WSCommandConnection* cc);

	void send(const String& msg)
	{
		if(active()) {
			socket.send(msg, WS_FRAME_TEXT);
		}
	}

	void send(JsonObjectConst json);

	void send(const void* data, size_t length)
	{
		if(active()) {
			socket.send(static_cast<const char*>(data), length, WS_FRAME_BINARY);
		}
	}

	void send(IDataSourceStream* data, ws_frame_type_t type = WS_FRAME_BINARY)
	{
		if(active()) {
			socket.send(data, type);
		}
	}

	// Permitted access type
	UserRole getAccess() const
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
			conn->setTimeOut(timeoutSecs);
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
		String s;
		if(conn) {
			s += conn->getRemoteIp().toString();
			s += ':';
			s += conn->getRemotePort();
		}
		return s;
	}

	uint32_t getCid() const
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
