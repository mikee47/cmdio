#pragma once

#include "ActionHandler.h"

#define EVENT_TRIGGER_MAP(XX)                                                                                          \
	XX(time)                                                                                                           \
	XX(sunrise)                                                                                                        \
	XX(sunset)

enum class Trigger {
	invalid = -1,
#define XX(type) type,
	EVENT_TRIGGER_MAP(XX)
#undef XX
};

String toString(Trigger trigger);

class Event : public LinkedObjectTemplate<Event>
{
public:
	using List = LinkedObjectListTemplate<Event>;
	using OwnedList = OwnedLinkedObjectListTemplate<Event>;

	Event(const char* id, JsonObject json);

	bool update(time_t now);

	time_t due() const
	{
		return mDue;
	}

	bool isActive() const
	{
		return mDue != 0;
	}

	const CStringArray& actions() const
	{
		return mActions;
	}

	bool operator==(const char* id) const
	{
		return mId == id;
	}

private:
	CString mId;
	Trigger mTrigger;
	time_t mDue{0};
	uint16_t mTime;  // In minutes
	int16_t mOffset; // In minutes
	CStringArray mActions;
};

class EventHandler : public WSCommandHandler
{
public:
	EventHandler(ActionHandler& actionHandler) : actionHandler(actionHandler)
	{
	}

	void onTimeChange(int adjustSecs);

	void begin();

	void update();

	String getMethod() const override;

	UserRole getMinAccess() const override
	{
		return UserRole::Manager;
	}

	PageInfo getPageInfo() const override
	{
		return {getMinAccess(), F("Events")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	void fileChange(const String& filename) override;

private:
	void reload();
	void trigger(Event& event);

	ActionHandler& actionHandler;
	Event::OwnedList events;
	Timer timer;
	Event* nextEvent{nullptr};
};
