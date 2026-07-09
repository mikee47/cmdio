#pragma once

#include "ActionHandler.h"

#define EVENT_TRIGGER_MAP(XX)                                                                                          \
	XX(time)                                                                                                           \
	XX(sunrise)                                                                                                        \
	XX(sunset)

enum class Trigger : int8_t {
	invalid = -1,
#define XX(type) type,
	EVENT_TRIGGER_MAP(XX)
#undef XX
};

String toString(Trigger trigger);
Trigger getTriggerFromString(const char* str);

#define EVENT_OPERATION_MAP(XX)                                                                                        \
	XX(lt)                                                                                                             \
	XX(lte)                                                                                                            \
	XX(eq)                                                                                                             \
	XX(gte)                                                                                                            \
	XX(gt)

enum class Operation : int8_t {
	invalid = -1,
#define XX(type) type,
	EVENT_OPERATION_MAP(XX)
#undef XX
};

String toString(Operation operation);
Operation getOperationfromString(const char* str);

class Event : public LinkedObjectTemplate<Event>
{
public:
	using List = LinkedObjectListTemplate<Event>;
	using OwnedList = OwnedLinkedObjectListTemplate<Event>;

	Event(const char* id, JsonObject json);

	/**
	 * @brief Update the next event time given the current time
	 * @retval bool true on success, false if event is not time-based
	 */
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

	const char* getId() const
	{
		return mId.c_str();
	}

	Trigger getTrigger() const
	{
		return mTrigger;
	}

	Operation getOperation() const
	{
		return mOperation;
	}

	int getValue() const
	{
		return mValue;
	}

	/**
	 * @brief Compare value for external event
	 */
	bool compare(int value)
	{
		switch(mOperation) {
		case Operation::invalid:
			return false;
		case Operation::lt:
			return value < mValue;
		case Operation::lte:
			return value <= mValue;
		case Operation::eq:
			return value == mValue;
		case Operation::gte:
			return value >= mValue;
		case Operation::gt:
			return value > mValue;
		}
		return false;
	}

	/**
	 * @brief Match an external event against value
	 */
	bool match(Trigger trigger, int value)
	{
		return trigger == mTrigger && compare(value);
	}

private:
	CString mId;
	Trigger mTrigger;
	Operation mOperation;
	uint32_t mDue{0};
	uint16_t mTime;  // In minutes
	int16_t mOffset; // In minutes
	int mValue;
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

	ACL getAccess() const override
	{
		return {UserRole::Manager, UserRole::Manager};
	}

	PageInfo getPageInfo() const override
	{
		return {getAccess(), F("Events")};
	}

	void handleMessage(WSCommandConnection* connection, JsonObject json) override;

	void fileChange(const String& filename) override;

	const Event::OwnedList& getEvents() const
	{
		return events;
	}

	/**
	 * @brief Match and trigger on external event
	 * @param trigger Trigger to match
	 * @param value Value to compare
	 * @retval unsigned Number of events triggered
	 */
	unsigned match(Trigger trigger, int value);

private:
	void reload();
	void trigger(Event& event);

	ActionHandler& actionHandler;
	Event::OwnedList events;
	Timer timer;
	Event* nextEvent{nullptr};
};
