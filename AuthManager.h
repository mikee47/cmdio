/*
 * AuthManager.h
 *
 *  Created on: 11 June 2018
 *      Author: mikee47
 *
 * Manages user authentication.
 *
 */

#ifndef __AUTH_MANAGER_H
#define __AUTH_MANAGER_H

#include "CommandHandler.h"

typedef Delegate<void(WSCommandConnection* connection, JsonObject& json)> login_callback_t;

class AuthManager: public WSCommandHandler
{
public:

	void onLoginComplete(login_callback_t callback)
	{
		m_onLoginComplete = callback;
	}

	/* CCommandHandler */

	String getMethod() const;

	UserRole minAccess() const
	{
		return UserRole::None;
	}

	static UserRole authenticateUser(const char* username, const char* password);
	void handleMessage(WSCommandConnection* connection, JsonObject& json);

private:
	void login(WSCommandConnection* connection, JsonObject& json);

private:
	login_callback_t m_onLoginComplete;

};

#endif // __AUTH_MANAGER_H
