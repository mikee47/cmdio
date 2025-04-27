/*
 * TimeManager.cpp
 *
 *  Created on: 25 Jun 2018
 *      Author: mikee47
 */

#include "TimeManager.h"
#include <tzdata.h>

DEFINE_FSTR(ATTR_LATITUDE, "latitude")
DEFINE_FSTR(ATTR_LONGITUDE, "longitude")
DEFINE_FSTR(STR_BST, "BST")
DEFINE_FSTR(STR_GMT, "GMT")
DEFINE_FSTR(TIME_SUNRISE, "sunrise")
DEFINE_FSTR(TIME_SUNSET, "sunset")
DEFINE_FSTR(TIME_DUSK, "dusk")
DEFINE_FSTR(TIME_DAWN, "dawn")
DEFINE_FSTR(TIME_NOW, "now")

TimeManager timeManager;

String toString(TimeType type)
{
	switch(type) {
#define XX(type, comment)                                                                                              \
	case TimeType::type:                                                                                               \
		return F(#type);
		TIMETYPE_MAP(XX)
#undef XX
	default:
		return nullptr;
	}
}

TimeType decodeTimeStr(const char* str, int& seconds)
{
	seconds = 0;

	if(str == nullptr) {
		return TimeType::invalid;
	}

	if(TIME_NOW == str) {
		return TimeType::now;
	}

	if(TIME_SUNRISE == str || TIME_DAWN == str) {
		return TimeType::sunrise;
	}

	if(TIME_SUNSET == str || TIME_DUSK == str) {
		return TimeType::sunset;
	}

	// hh:nn[:ss]
	char* ptr;
	int secs = strtol(str, &ptr, 10) * SECS_PER_HOUR;
	if(*ptr != ':') {
		return TimeType::invalid;
	}
	++ptr;
	secs += strtol(ptr, &ptr, 10) * SECS_PER_MIN;
	if(*ptr == ':') {
		++ptr;
		secs += strtol(ptr, &ptr, 10);
	}
	if(*ptr != '\0') {
		return TimeType::invalid;
	}

	seconds = secs;
	return TimeType::absolute;
}

void TimeManager::configure(JsonObjectConst config)
{
	auto ref = solarCalc.getRef();
	Json::getValue(config[ATTR_LATITUDE], ref.latitude);
	Json::getValue(config[ATTR_LONGITUDE], ref.longitude);
	solarCalc.setRef(ref);
	zone = TZ::Europe::London();
}

void TimeManager::update(time_t timeUTC)
{
	bool changed = !SystemClock.isSet();

	time_t tCur = SystemClock.now(eTZ_UTC);
	debug_i("TimeManager::update(%s), current: %s", timeStr(timeUTC).c_str(), timeStr(tCur).c_str());

	if(bootTimeUTC == 0) {
		bootTimeUTC = timeUTC - (millis() / 1000U);
	}

	// Update system clock if it's drifted sufficiently
	if(abs(tCur - timeUTC) > MAX_CLOCK_DRIFT) {
		changed = true;
	}

	if(!changed) {
		return;
	}

	// Time zone difference also accounts for DST
	int diff = zone.toLocal(timeUTC) - timeUTC;
	debug_i("TZ diff = %d secs", diff);
	SystemClock.setTimeZoneOffset(diff);
	SystemClock.setTime(timeUTC, eTZ_UTC);

#if DEBUG_BUILD

	debug_i("Now:     %s", timeStr(TimeType::now, 0).c_str());
	debug_i("Sunrise: %s", timeStr(TimeType::sunrise, 0).c_str());
	debug_i("Sunset:  %s", timeStr(TimeType::sunset, 0).c_str());

#endif

	if(changeCallback) {
		changeCallback(timeUTC - tCur);
	}
}

/*
 * All calculated in local time:
 *
 *   now:      The current time, plus offset_secs
 *   absolute: Today's date plus offset_secs since midnight
 *   sunrise:  The next sunrise, plus offset_secs
 *   sunset:   The next sunset, plus offset_secs
 */
time_t TimeManager::getTime(TimeType timetype, int offset_secs)
{
	if(timetype == TimeType::boot) {
		return zone.toLocal(bootTimeUTC + offset_secs);
	}

	time_t tNow = SystemClock.now(eTZ_Local);

	if(timetype == TimeType::now) {
		return tNow + offset_secs;
	}

	DateTime dt(tNow);
	dt.Hour = 0;
	dt.Minute = 0;
	dt.Second = 0;

	if(timetype == TimeType::absolute) {
		time_t t = dt + offset_secs;
		// If time has already passed, then make it tomorrow.
		if(tNow > t) {
			t += SECS_PER_DAY;
		}
		return t;
	}

	if(timetype == TimeType::sunrise || timetype == TimeType::sunset) {
		/*
		 * We use the y/m/d from local time for sunrise/sunset calculations, and the solar calculator
		 * returns the time from midnight in UTC for that day. We therefore need to adjust this
		 * to account for timezone and daylight savings.
		 */
		offset_secs +=
			SECS_PER_MIN * solarCalc.sunRiseSet(timetype == TimeType::sunrise, dt.Year, dt.Month + 1, dt.Day);
		//    return toLocal(t + SECS_PER_MIN * m_solarCalc.sunrise(dt.Year, dt.Month + 1, dt.Day));

		time_t t = zone.toLocal(dt + offset_secs);
		// If time has already passed, then make it tomorrow
		if(t < tNow) {
			t = zone.toLocal(dt + offset_secs + SECS_PER_DAY);
		}
		return t;
	}

	// Unknown/invalid
	return 0;
}

String TimeManager::timeStr(time_t t)
{
	return DateTime(t).toFullDateTimeString() + String(' ') + zone.localTimeTag(t);
}
