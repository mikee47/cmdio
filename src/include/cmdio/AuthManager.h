/*
 * AuthManager.h
 *
 *  Created on: 11 June 2018
 *      Author: mikee47
 *
 * Manages user authentication.
 *
 */

#pragma once

#include "CommandHandler.h"

typedef Delegate<void(WSCommandConnection* connection, JsonObject json)> LoginDelegate;

class AuthManager : public WSCommandHandler
{
public:
	void onLoginComplete(LoginDelegate callback)
	{
		loginCompleteCallback = callback;
	}

	static UserRole authenticateUser(const char* username, const char* password);

	/* WSCommandHandler */

	String getMethod() const override;

	ACL getAccess() const override
	{
		return {UserRole::None, UserRole::Manager};
	}

	PageInfo getPageInfo() const override
	{
		return {{UserRole::Manager, UserRole::Manager}, "Users"};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	void disconnected(WSCommandConnection& connection);

private:
	void login(WSCommandConnection* connection, JsonObject json);
	void listUsers(WSCommandConnection* connection, JsonObject json);
	void updateUser(WSCommandConnection* connection, JsonObject json);

private:
	LoginDelegate loginCompleteCallback;
};

extern AuthManager authManager;
