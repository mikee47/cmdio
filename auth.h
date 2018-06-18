/*
 * auth.h
 *
 *  Created on: 11 June 2018
 *      Author: Mike
 */

#ifndef __AUTH_H
#define __AUTH_H

#include "cmdhandler.h"


typedef Delegate<void(command_connection_t connection, JsonObject& json)> login_callback_t;


class CAuthManager: public CCommandHandler
{
  private:
    login_callback_t m_onLoginComplete;

  private:
    void login(command_connection_t connection, JsonObject& json);

  public:

    void onLoginComplete(login_callback_t callback)
    {
      m_onLoginComplete = callback;
    }

    /* CCommandHandler */

    String getMethod() const;

    access_type_t minAccess() const
    {
      return access_none;
    }

    static access_type_t authenticateUser(const char* username, const char* password);
    void handleMessage(command_connection_t connection, JsonObject& json);
};



#endif // __NETWORK_H
