/*
 * AuthManager.cpp
 *
 *  Created on: 11 June 2018
 *      Author: mikee47
 */

#include <WString.h>
#include "JsonConfigFile.h"

#include "AuthManager.h"

#include "FileManager.h"
#include "NetworkManager.h"

// Global instance
AuthManager authManager;

static DEFINE_FSTR(METHOD_AUTH, "auth")

// Login
static DEFINE_FSTR(COMMAND_LOGIN, "login")
static DEFINE_FSTR(ATTR_USERS, "users")

static DEFINE_FSTR(FILE_AUTH, ".auth.json")

/*
 * Given a username and password check the users list to see if there is a match.
 *
 * @returns access level permitted
 */
UserRole AuthManager::authenticateUser(const char* username, const char* password)
{
	if (username == nullptr)
		username = "";
	if (password == nullptr)
		password = "";

	JsonConfigFile config;
	if (config.load(FILE_AUTH)) {
		JsonArray& users = config[ATTR_USERS];
		for (auto& user : users) {
			if (strcasecmp(user[ATTR_NAME], username))
				continue;

			if (strcmp(user[ATTR_PASSWORD], password)) {
				debug_i("password mismatch");
				break;
			}

			auto role = getUserRole(user[ATTR_ACCESS].asString(), UserRole::None);
			debug_i("Role = %u", role);
			return role;
		}
	}

	return UserRole::None;
}

String AuthManager::getMethod() const
{
	return METHOD_AUTH;
}

// Don't overwrite access unless authenticated
void AuthManager::login(command_connection_t connection, JsonObject& json)
{
	UserRole access = UserRole::None;

	const char* name = json[ATTR_NAME];
	const char* password = json[ATTR_PASSWORD];

	/*
	 * If we're in AP mode then blank login on local subnet gets user access
	 * to permit network scanning and configuration.
	 */

	if (!name && !password && WifiAccessPoint.isEnabled()) {
		IPAddress ip = connection->getRemoteIp();
		if (ip.compare(WifiAccessPoint.getIP(), WifiAccessPoint.getNetworkMask()))
			access = UserRole::User;
		else
			debug_w("Different subnets, default access withheld");
	}

	if (access == UserRole::None)
		access = authenticateUser(name, password);

	if (access == UserRole::None)
		setError(json, ioe_access_denied);
	else {
		setSuccess(json);

		// OK, user/password matches
		connection->setAccess(access);
		char buf[10];
		json[ATTR_ACCESS] = String(userRoleToStr(access, buf, sizeof(buf)));

		if (m_onLoginComplete)
			m_onLoginComplete(connection, json);
	}
}

void AuthManager::handleMessage(command_connection_t connection, JsonObject& json)
{
	const char* command = json[ATTR_COMMAND];

	if (COMMAND_LOGIN == command) {
		login(connection, json);
	}

	// Don't include password in response
	json.remove(ATTR_PASSWORD);
}

