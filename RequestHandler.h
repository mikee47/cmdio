#pragma once

#include <WString.h>
#include <cmdio/CommandHandler.h>

class RequestHandler : public WSCommandHandler
{
public:
	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::User;
	}

	PageInfo getPageInfo() const override
	{
		return {getMinAccess(), F("IO Control")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;
};
