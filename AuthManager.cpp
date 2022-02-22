/*
 * AuthManager.cpp
 *
 *  Created on: 11 June 2018
 *      Author: mikee47
 */

#include <WString.h>

#include "AuthManager.h"
#include "NetworkManager.h"

// Global instance
AuthManager authManager;

DEFINE_FSTR_LOCAL(METHOD_AUTH, "auth");

// Login
DEFINE_FSTR_LOCAL(COMMAND_LOGIN, "login");
DEFINE_FSTR_LOCAL(ATTR_USERS, "users");
DEFINE_FSTR_LOCAL(ATTR_ACCESS, "access");
// List users
DEFINE_FSTR_LOCAL(COMMAND_LIST, "list");

DEFINE_FSTR_LOCAL(FILE_AUTH, "config/.auth.json");

/*
 * Given a username and password check the users list to see if there is a match.
 *
 * @returns access level permitted
 */
UserRole AuthManager::authenticateUser(const char* username, const char* password)
{
	DynamicJsonDocument config(1024);
	Json::loadFromFile(config, FILE_AUTH);
	JsonObject users = config[ATTR_USERS];

	if(users.size() == 0) {
		// If unconfigured or corrupted, need a way in
		if(F("admin-default") == username) {
			if(F("please-configure-users") == password) {
				return UserRole::Admin;
			}
		}

		return UserRole::None;
	}

	String name(username);
	name.toLowerCase();
	JsonObject user = users[name];
	if(!user) {
		debug_w("Unknown user '%s'", name.c_str());
		return UserRole::None;
	}

	if(user[ATTR_PASSWORD].as<String>() != password) {
		debug_w("Password mismatch");
		return UserRole::None;
	}

	auto role = getUserRole(user[ATTR_ACCESS].as<const char*>(), UserRole::None);
	debug_i("Role = %u", role);
	return role;
}

/*
 * Provide a list of users.
 * Only managers and above can retrieve this.
 */
void AuthManager::listUsers(WSCommandConnection* connection, JsonObject json)
{
	if(connection->getAccess() < UserRole::Manager) {
		return (void)IO::setError(json, IO::Error::access_denied);
	}

	DynamicJsonDocument config(1024);
	Json::loadFromFile(config, FILE_AUTH);
	auto users = json.createNestedObject(ATTR_USERS);
	for(JsonPair entry : config[ATTR_USERS].as<JsonObject>()) {
		auto user = users.createNestedObject(entry.key());
		user[ATTR_ACCESS] = entry.value()[ATTR_ACCESS];
	}
}

String AuthManager::getMethod() const
{
	return METHOD_AUTH;
}

// Don't overwrite access unless authenticated
void AuthManager::login(WSCommandConnection* connection, JsonObject json)
{
	UserRole access = UserRole::None;

	const char* name = json[ATTR_NAME];
	const char* password = json[ATTR_PASSWORD];

	/*
	 * If we're in AP mode then blank login on local subnet gets user access
	 * to permit network scanning and configuration.
	 */

	if(name == nullptr && password == nullptr && WifiAccessPoint.isEnabled()) {
		IpAddress ip = connection->getRemoteIp();
		if(ip.compare(WifiAccessPoint.getIP(), WifiAccessPoint.getNetworkMask())) {
			access = UserRole::User;
		} else {
			debug_w("Different subnets, default access withheld");
		}
	}

	if(access == UserRole::None) {
		access = authenticateUser(name, password);
	}

	if(access == UserRole::None) {
		IO::setError(json, IO::Error::access_denied);
	} else {
		IO::setSuccess(json);

		// OK, user/password matches
		connection->setAccess(access);
		json[ATTR_ACCESS] = toString(access);

		if(loginCompleteCallback) {
			loginCompleteCallback(connection, json);
		}
	}
}

void AuthManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];

	if(COMMAND_LOGIN == command) {
		login(connection, json);
	} else if(COMMAND_LIST == command) {
		listUsers(connection, json);
	}

	// Don't include password in response
	json.remove(ATTR_PASSWORD);
}
