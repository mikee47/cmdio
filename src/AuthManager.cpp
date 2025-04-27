/*
 * AuthManager.cpp
 *
 *  Created on: 11 June 2018
 *      Author: mikee47
 */

#include <WString.h>

#include "include/cmdio/AuthManager.h"
#include "include/cmdio/NetworkManager.h"
#include <SystemClock.h>
#include <IO/Strings.h>

#define MIN_USERNAME_LENGTH 3
#define MIN_PASSWORD_LENGTH 5

// Global instance
AuthManager authManager;

DEFINE_FSTR_LOCAL(METHOD_AUTH, "auth");

// Login
DEFINE_FSTR_LOCAL(COMMAND_LOGIN, "login");
DEFINE_FSTR_LOCAL(ATTR_USERS, "users");
DEFINE_FSTR_LOCAL(ATTR_ACCESS, "access");
// List users
DEFINE_FSTR_LOCAL(COMMAND_LIST, "list");
DEFINE_FSTR_LOCAL(ATTR_ROLES, "roles");
DEFINE_FSTR_LOCAL(ATTR_ROLE, "role");
//
DEFINE_FSTR_LOCAL(COMMAND_UPDATE_USER, "update-user");
DEFINE_FSTR_LOCAL(FILE_AUTH, "config/.auth.json");
DEFINE_FSTR_LOCAL(FILE_AUTH_LOG, "config/auth.log");

#define LOG(msg) logAppend(F(msg))
#define LOG2(msg, arg) logAppend(F(msg), arg)

namespace
{
void logAppend(const String& msg, const String& arg = nullptr)
{
#ifdef ENABLE_AUTH_LOG
	File file;
	if(!file.open(FILE_AUTH_LOG, File::WriteOnly | File::Create)) {
		return;
	}
	file.seek(0, SeekOrigin::End);
	DateTime now = SystemClock.now();
	String s;
	s += now.toISO8601();
	s += '\t';
	s += msg;
	if(arg) {
		s += '\t';
		s += arg;
	}
	s += "\n";

	auto pos = file.tell();
	file.write(s.c_str(), s.length());
	if(pos == 0) {
		file.setacl({UserRole::Admin, UserRole::Admin});
	}
#else
	(void)msg;
	(void)arg;
#endif
}

} // namespace

/*
 * Given a username and password check the users list to see if there is a match.
 *
 * @returns access level permitted
 */
UserRole AuthManager::authenticateUser(const char* username, const char* password)
{
	LOG2("user", username);

	DynamicJsonDocument config(1024);
	if(!Json::loadFromFile(config, FILE_AUTH)) {
		LOG("Corrupt FILE_AUTH");
	}
	JsonObject users = config[ATTR_USERS];

	if(users.size() == 0) {
		LOG("No users");
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

	const char* requiredPassword = user[ATTR_PASSWORD];
	if(requiredPassword == nullptr || strcmp(requiredPassword, password) != 0) {
		debug_w("Password mismatch");
		return UserRole::None;
	}

	const char* access = user[ATTR_ACCESS];
	auto role = getUserRole(access, UserRole::None);
	debug_i("Role = %u", role);
	return role;
}

/*
 * Provide a list of users.
 * Only managers and above can retrieve this.
 */
void AuthManager::listUsers(WSCommandConnection* connection, JsonObject json)
{
	auto access = connection->getAccess();
	if(access < UserRole::Manager) {
		return (void)IO::setError(json, IO::Error::access_denied);
	}

	DynamicJsonDocument config(2048);
	Json::loadFromFile(config, FILE_AUTH);
	auto usersToSend = json.createNestedObject(ATTR_USERS);
	for(JsonPair entry : config[ATTR_USERS].as<JsonObject>()) {
		auto userAccess = entry.value()[ATTR_ACCESS].as<const char*>();
		if(userAccess == nullptr || access < getUserRole(userAccess, UserRole::MAX)) {
			continue;
		}
		JsonObject userToSend = usersToSend.createNestedObject(entry.key());
		userToSend[ATTR_ROLE] = String(userAccess);
	}

	auto roles = json.createNestedArray(ATTR_ROLES);
	for(unsigned i = unsigned(UserRole::User); i <= unsigned(access); ++i) {
		roles.add(toString(UserRole(i)));
	}

	IO::setSuccess(json);
}

void AuthManager::updateUser(WSCommandConnection* connection, JsonObject json)
{
	auto access = connection->getAccess();
	if(access < UserRole::Manager) {
		return (void)IO::setError(json, IO::Error::access_denied);
	}

	String username = json[ATTR_NAME].as<const char*>();
	username.toLowerCase();
	LOG2("user", username);
	auto newPassword = json[ATTR_PASSWORD].as<const char*>();
	auto newPasswordLength = (newPassword == nullptr) ? 0 : strlen(newPassword);
	auto newRole = getUserRole(json[ATTR_ROLE].as<const char*>(), UserRole::None);

	DynamicJsonDocument doc(2048);
	if(!Json::loadFromFile(doc, FILE_AUTH)) {
		return (void)IO::setError(json, IO::Error::no_mem);
	}
	auto config = doc.as<JsonObject>();
	JsonObject users = config[ATTR_USERS];
	JsonObject user = users[username];

	bool newUser = user.isNull();

	if(!newUser) {
		auto role = getUserRole(user[ATTR_ACCESS].as<const char*>(), UserRole::MAX);
		if(role > access) {
			return (void)IO::setError(json, IO::Error::access_denied);
		}
	}

	if(newRole == UserRole::None) {
		users.remove(username);
	} else {
		if(newRole > access) {
			return (void)IO::setError(json, IO::Error::access_denied);
		}
		if(newUser && username.length() < MIN_USERNAME_LENGTH) {
			return (void)IO::setError(json, IO::Error::bad_param);
		}
		// Password length enforced for new users, can be empty for existing users
		if((newUser || newPasswordLength > 0) && newPasswordLength < MIN_PASSWORD_LENGTH) {
			return (void)IO::setError(json, IO::Error::bad_param);
		}

		if(newUser) {
			user = users.createNestedObject(username);
		}
		String s = toString(newRole);
		s.toLowerCase();
		user[ATTR_ACCESS] = s;
		if(newPasswordLength != 0) {
			user[ATTR_PASSWORD] = newPassword;
		}
	}

	if(!Json::saveToFile(config, FILE_AUTH)) {
		return (void)IO::setError(json, IO::Error::file);
	}

	IO::setSuccess(json);
}

String AuthManager::getMethod() const
{
	return METHOD_AUTH;
}

// Don't overwrite access unless authenticated
void AuthManager::login(WSCommandConnection* connection, JsonObject json)
{
	const char* name = json[ATTR_NAME];
	const char* password = json[ATTR_PASSWORD];

	/*
	 * If we're in AP mode then blank login on local subnet gets user access
	 * to permit network scanning and configuration.
	 *
	 * This doesn't help when connected via local bridge as all clients appear local.
	 */
	UserRole access{UserRole::None};
	if(name == nullptr && password == nullptr && WifiAccessPoint.isEnabled()) {
		IpAddress ip = connection->getRemoteIp();
		if(ip.compare(WifiAccessPoint.getIP(), WifiAccessPoint.getNetworkMask())) {
			access = UserRole::User;
		} else {
			debug_w("Different subnets, default access withheld");
		}
	} else {
		access = authenticateUser(name, password);
	}

	if(access == UserRole::None) {
		return (void)IO::setError(json, IO::Error::access_denied);
	}

	IO::setSuccess(json);

	// OK, user/password matches
	connection->setAccess(access);
	json[ATTR_ACCESS] = toString(access);

	if(loginCompleteCallback) {
		loginCompleteCallback(connection, json);
	}
}

void AuthManager::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];

	logAppend(command, String(connection->getCid(), HEX));

	if(COMMAND_LOGIN == command) {
		login(connection, json);
	} else if(COMMAND_LIST == command) {
		listUsers(connection, json);
	} else if(COMMAND_UPDATE_USER == command) {
		updateUser(connection, json);
	} else {
		IO::setError(json, IO::Error::bad_command);
	}

	logAppend(json[IO::FS_status], json[IO::FS_error][IO::FS_text].as<const char*>());

	// Don't include password in response
	json.remove(ATTR_PASSWORD);
}

void AuthManager::disconnected(WSCommandConnection& connection)
{
	LOG2("CLOSE", String(connection.getCid(), HEX));
}
