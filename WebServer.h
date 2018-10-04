/*
 * WebServer.h
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#ifndef __WEBSERVER_H
#define __WEBSERVER_H

#include "Network/HttpServer.h"
#include "CommandHandler.h"
#include <functional>


DECLARE_FSTR(FILE_INDEX_HTML)

/** @brief  Callback function for web server access authorisation
 *
 * @param filename The file for which access is being requested
 * @param access The authorised request access level
 * @returns true if file may be accessed
 */
typedef std::function<bool(const String& filename, UserRole access)> file_access_callback_t;

class WebServer: public ICommandHandler
{
public:
	bool start();
	void stop();

	bool restart();

	/* CCommandHandler */
	String getMethod() const;

	UserRole minAccess() const
	{
		return UserRole::Admin;
	}

	void handleMessage(command_connection_t connection, JsonObject& json);

private:
	HttpServer* m_server = nullptr;

private:
	int requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response);
	void sendFile(const String& filename, const String& cid, HttpResponse& response);
};

void startWebServer();
void stopWebServer();

#endif // __WEBSERVER_H
