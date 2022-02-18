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

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;
};
