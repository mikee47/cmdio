#pragma once

#include <WString.h>
#include "CommandHandler.h"
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
	 * @param actions List of action ids
	 * @param byName Pass true to interprets actions by name instead of ID
	 * @retval unsigned Number of actions found
	 *
	 * Actions are matched without case sensitivity.
	 */
	unsigned trigger(const CStringArray& actions, bool byName = false);

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

private:
	unsigned addRequests(const CStringArray& actions, bool byName);
	void executeRequest();
	bool check(IO::ErrorCode err);

	CStringArray requestQueue;
	uint16_t errorCount{0};
	uint16_t requestCount{0};
	IO::ErrorCode lastError{};
	Timer timer; ///< Enforce interval between requests
};
