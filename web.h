/*
 * web.h
 *
 *  Created on: 6 Jun 2018
 *      Author: Mike
 */

#ifndef __WEB_H
#define __WEB_H

#include <SmingCore/SmingCore.h>

#include "cmdhandler.h"

DECLARE_STRING_P(FILE_INDEX_HTML)


/*
 * Minor customisation to HttpServer
 */
class HttpServerEx: public HttpServer
{
  public:
    HttpServerEx()
    {
      HttpServerSettings settings;
      settings.maxActiveConnections = 10,
      settings.keepAliveSeconds = 5,
      settings.minHeapSize = -1,
      settings.useDefaultBodyParsers = true,
      #ifdef ENABLE_SSL
      settings.sslSessionCacheSize = 10
      #endif
      configure(settings);
    }

    /*
     * connection list is required to enumerate connected clients.
     * It's protected though.
     */
    Vector<TcpConnection*>& connections()
    {
      return HttpServer::connections;
    }

};


/** @brief  Callback function for web server access authorisation
 *
 * @param filename The file for which access is being requested
 * @param access The authorised request access level
 * @returns true if file may be accessed
 */
typedef std::function<bool (const String& filename, access_type_t access)> file_access_callback_t;


class CWebServer: public CCommandHandler
{
  private:
    HttpServerEx* m_server;

  private:
    int requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response);
    bool sendFile(const String& filename, const String& cid, HttpResponse& response);

  public:
    bool start();
    void stop();

    bool restart();

    /* CCommandHandler */
    String getMethod() const;

    access_type_t minAccess() const
    {
      return access_admin;
    }

    void handleMessage(command_connection_t connection, JsonObject& json);
};


void startWebServer();
void stopWebServer();


#endif // __WEB_H
