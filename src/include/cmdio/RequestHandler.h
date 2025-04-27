#pragma once

#include <WString.h>
#include "CommandHandler.h"

class RequestHandler : public WSCommandHandler
{
public:
	String getMethod() const override;

	ACL getAccess() const override
	{
		return {UserRole::User, UserRole::User};
	}

	PageInfo getPageInfo() const override
	{
		return {getAccess(), F("IO Control")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;
};
