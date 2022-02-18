#include "EventHandler.h"
#include <core/TimeManager.h>
#include <FlashString/Vector.hpp>

DEFINE_FSTR_LOCAL(METHOD_EVENT, "events")
DEFINE_FSTR_LOCAL(FILE_EVENT_CONFIG, "config/events.json")
DEFINE_FSTR_LOCAL(ATTR_ENABLE, "enable")
DEFINE_FSTR_LOCAL(ATTR_OFFSET, "offset")
DEFINE_FSTR_LOCAL(ATTR_ACTIONS, "actions")
DEFINE_FSTR_LOCAL(COMMAND_TIME, "time")
#define ATTR_TRIGGER COMMAND_TRIGGER
#define ATTR_TIME COMMAND_TIME

#define XX(type) DEFINE_FSTR_LOCAL(TRIGGER_##type, #type)
EVENT_TRIGGER_MAP(XX)
#undef XX

#define XX(type) &TRIGGER_##type,
DEFINE_FSTR_VECTOR(triggerTypes, FlashString, EVENT_TRIGGER_MAP(XX))
#undef XX

String toString(Trigger trigger)
{
	return triggerTypes[unsigned(trigger)];
}

Event::Event(const char* id, JsonObject json) : mId(id)
{
	const char* trigger = json[ATTR_TRIGGER];
	mTrigger = Trigger(triggerTypes.indexOf(trigger));
	mTime = json[ATTR_TIME];
	mOffset = json[ATTR_OFFSET].as<int>();
	for(const char* action : json[ATTR_ACTIONS].as<JsonArray>()) {
		mActions.add(action);
	}

	if(mTrigger == Trigger::invalid) {
		debug_w("Invalid trigger: '%s'", trigger);
	}
}

bool Event::update(time_t now)
{
	switch(mTrigger) {
	case Trigger::sunrise:
		mDue = timeManager.getTime(TimeType::sunrise, mOffset * 60);
		break;
	case Trigger::sunset:
		mDue = timeManager.getTime(TimeType::sunset, mOffset * 60);
		break;
	case Trigger::time: {
		DateTime dt = SystemClock.now(eTZ_Local);
		dt.Hour = mTime / 60;
		dt.Minute = mTime % 60;
		dt.Second = 0;
		dt.Milliseconds = 0;
		mDue = timeManager.toLocal(dt + mOffset * 60);
		if(mDue <= now) {
			mDue += SECS_PER_DAY;
		}
		break;
	}
	default:
		mDue = 0;
		return false;
	}

#if DEBUG_VERBOSE_LEVEL >= INFO
	String s;
	for(auto act : mActions) {
		if(s) {
			s += ',';
		}
		s += act;
	}
	debug_i("Event %s, trigger %s, offset %d, time %s, [%s]", mId.c_str(), toString(mTrigger).c_str(), mOffset,
			DateTime(mDue).toFullDateTimeString().c_str(), s.c_str());
#endif
	return true;
}

void EventHandler::begin()
{
	Timer::checkInterval<NanoTime::Seconds, SECS_PER_DAY * 365>();
	reload();
}

void EventHandler::onTimeChange(int adjustSecs)
{
	System.queueCallback([this]() { update(); });

	if(events.isEmpty()) {
		reload();
	}
}

/*
Keep a list of events in RAM.
Actions are loaded and executed only when required.

TODO:
If clock is slow, and gets an update then due event may be missed.
Need to check for this and fire the event when conditions are met.
For example:
- event due in 3 minutes
- Clock moved forward by 1 hour
- 

*/
void EventHandler::reload()
{
	events.clear();

	DynamicJsonDocument config(4096);
	Json::loadFromFile(config, FILE_EVENT_CONFIG);
	for(JsonPair p : config.as<JsonObject>()) {
		const char* id = p.key().c_str();
		JsonObject json = p.value();
		if(json[ATTR_ENABLE].as<bool>()) {
			events.add(new Event(id, json));
		}
	}

	update();
}

void EventHandler::update()
{
	timer.stop();
	time_t nextDue{0};
	nextEvent = nullptr;
	auto now = SystemClock.now(eTZ_Local);

	for(auto& event : events) {
		if(!event.update(now)) {
			continue;
		}
		if(nextDue == 0 || event.due() < nextDue) {
			nextDue = event.due();
			nextEvent = &event;
		}
	}

	if(nextEvent == nullptr) {
		debug_i("[EVT] No events due");
		return;
	}

	debug_i("[EVT] Next due %s (%u seconds)", DateTime(nextDue).toFullDateTimeString().c_str(), nextDue - now);

	timer.setInterval<NanoTime::Seconds>(nextDue - now);
	timer.setCallback([this]() {
		assert(nextEvent != nullptr);
		trigger(*nextEvent);
	});
	timer.startOnce();
}

void EventHandler::trigger(Event& event)
{
	debug_i("[EVT] FIRE!");

	actionHandler.trigger(event.actions());

	// Queue next event
	update();
}

void EventHandler::handleMessage(WSCommandConnection* connection, JsonObject json)
{
	const char* command = json[ATTR_COMMAND];
	if(COMMAND_TIME == command) {
		json[TIME_SUNRISE] = uint32_t(timeManager.getTime(TimeType::sunrise));
		json[TIME_SUNSET] = uint32_t(timeManager.getTime(TimeType::sunset));
		return (void)IO::setSuccess(json);
	}

	if(COMMAND_TRIGGER == command) {
		auto id = json["id"].as<const char*>();
		auto event = std::find(events.begin(), events.end(), id);
		if(!event) {
			return (void)IO::setError(json, IO::Error::bad_param);
		}
		trigger(*event);
		return (void)IO::setPending(json);
	}

	IO::setError(json, IO::Error::bad_command);
}

void EventHandler::fileChange(const String& filename)
{
	if(filename == FILE_EVENT_CONFIG) {
		reload();
	}
}

String EventHandler::getMethod() const
{
	return METHOD_EVENT;
}
