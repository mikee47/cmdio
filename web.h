/*
 * web.h
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */

#ifndef __WEB_H
#define __WEB_H

#include <SmingCore.h>

#include "cmdhandler.h"

DECLARE_STRING_P(FILE_INDEX_HTML)



/** @brief  Callback function for web server access authorisation
 *
 * @param filename The file for which access is being requested
 * @param access The authorised request access level
 * @returns true if file may be accessed
 */
typedef std::function<bool (const String& filename, UserRole access)> file_access_callback_t;


class CWebServer: public CCommandHandler
{
  private:
    HttpServer* m_server = nullptr;

  private:
    int requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response);
    void sendFile(const String& filename, const String& cid, HttpResponse& response);

  public:
    bool start();
    void stop();

    bool restart();

    /* CCommandHandler */
    String getMethod() const;

    UserRole minAccess() const
    {
      return UserRole::admin;
    }

    void handleMessage(command_connection_t connection, JsonObject& json);
};


void startWebServer();
void stopWebServer();


#endif // __WEB_H
