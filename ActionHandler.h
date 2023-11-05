#pragma once

#include <WString.h>
#include <cmdio/CommandHandler.h>
#include <Timer.h>

DECLARE_FSTR(COMMAND_TRIGGER)

class ActionHandler : public WSCommandHandler
{
public:
	String getMethod() const override;

	ACL getAccess() const override
	{
		return {UserRole::Manager, UserRole::Manager};
	}

	PageInfo getPageInfo() const override
	{
		return {getAccess(), F("Actions")};
	}

	/**
	 * @brief Trigger an action
	 * @brief actions List of action ids
	 */
	void trigger(const CStringArray& actions);

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

private:
	void addRequests(const CStringArray& actions);
	void executeRequest();
	bool check(IO::ErrorCode err);

	CStringArray requestQueue;
	uint16_t errorCount{0};
	uint16_t requestCount{0};
	IO::ErrorCode lastError{};
	Timer timer; ///< Enforce interval between requests
};
