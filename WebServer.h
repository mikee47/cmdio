/*
 * WebServer.h
 *
 *  Created on: 6 Jun 2018
 *      Author: mikee47
 */

#pragma once

#include <Network/HttpServer.h>
#include "CommandHandler.h"

DECLARE_FSTR(FILE_INDEX_HTML)

/**
 * @brief Callback type for intercepting web requests
 * @param connection The connection, containing request and response objects
 * @retval bool Return true if request was handled, false if not interested
 */
using WebRequestDelegate = Delegate<bool(HttpServerConnection& connection)>;

class WebServer : public WSCommandHandler
{
public:
	bool start(const HttpServerSettings& settings);
	void stop();

	bool restart();

	/* WSCommandHandler */
	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::Admin;
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	void onRequestComplete(WebRequestDelegate delegate)
	{
		requestCompleteDelegate = delegate;
	}

private:
	int requestComplete(HttpServerConnection& connection, HttpRequest& request, HttpResponse& response);
	void sendFile(const String& filename, const String& format, const String& cid, HttpResponse& response);

private:
	HttpServer* server = nullptr;
	WebRequestDelegate requestCompleteDelegate;
};

void startWebServer();
void stopWebServer();
