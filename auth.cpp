/*
 * auth.cpp
 *
 *  Created on: 11 June 2018
 *      Author: Mike
 */


#include <SmingCore/SmingCore.h>

#include <auth.h>
#include <WString_P.h>
#include <network.h>
#include <configfile.h>


// Global instance
CAuthManager authManager;


static DEFINE_STRING_P(METHOD_AUTH, "auth")

// Login
static DEFINE_STRING_P(COMMAND_LOGIN, "login")
static DEFINE_STRING_P(ATTR_USERS, "users")
static DEFINE_STRING_P(ATTR_ACCESS, "access")

static DEFINE_STRING_P(FILE_AUTH, ".auth.json")



/*
 * Given a username and password check the users list to see if there is a match.
 *
 * @returns access level permitted
 */
access_type_t CAuthManager::authenticateUser(const char* username, const char* password)
{
  CConfigFile config;
  if (config.init(FILE_AUTH())) {
    JsonArray& users = config.root()[ATTR_USERS()];
    for (auto& user: users) {
      if (strcasecmp(user[ATTR_NAME()], username))
        continue;

      if (strcmp(user[ATTR_PASSWORD()], password))
        break;

      return getAccessType(user[ATTR_ACCESS()]);
    }
  }

  return access_none;
}


String CAuthManager::getMethod() const
{
  return METHOD_AUTH();
}


// Don't overwrite access unless authenticated
void CAuthManager::login(command_connection_t connection, JsonObject& json)
{
  access_type_t access = access_none;

  const char* name = json[ATTR_NAME()];
  const char* password = json[ATTR_PASSWORD()];

  /*
   * If we're in AP mode then blank login on local subnet gets user access
   * to permit network scanning and configuration.
   */

  if (!name && !password && WifiAccessPoint.isEnabled()) {
    IPAddress ip = connection->getHttpConnection().getRemoteIp();
    if (ip.compare(WifiAccessPoint.getIP(), WifiAccessPoint.getNetworkMask()))
      access = access_user;
    else
      debug_w("Different subnets, default access withheld");
  }

  if (access == access_none)
    access = authenticateUser(name, password);

  if (access == access_none)
    setError(json);
  else {
    setSuccess(json);

    // OK, user/password matches
    connection->setAccess(access);
    json[ATTR_ACCESS()] = accessTypeToStr(access);

    if (m_onLoginComplete)
      m_onLoginComplete(connection, json);
  }
}


void CAuthManager::handleMessage(command_connection_t connection, JsonObject& json)
{
  const char* command = json[ATTR_COMMAND()];

  if (COMMAND_LOGIN() == command) {
    login(connection, json);
  }

  // Don't include password in response
  json.remove(ATTR_PASSWORD());
}

