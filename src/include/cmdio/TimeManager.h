#pragma once

#include <SystemClock.h>
#include <SolarCalculator.h>
#include <Timezone.h>
#include <ArduinoJson.h>

// Allow some drift on RTC so updates aren't too frequent
#define MAX_CLOCK_DRIFT 10 // in seconds

DECLARE_FSTR(ATTR_LATITUDE)
DECLARE_FSTR(ATTR_LONGITUDE)
DECLARE_FSTR(STR_BST)
DECLARE_FSTR(STR_GMT)
DECLARE_FSTR(TIME_SUNRISE)
DECLARE_FSTR(TIME_SUNSET)
DECLARE_FSTR(TIME_DUSK)
DECLARE_FSTR(TIME_DAWN)
DECLARE_FSTR(TIME_NOW)

// Callback gets time adjustment in seconds as (tNew - tCur)
typedef Delegate<void(int adjustSecs)> TimeChangeDelegate;

#define TIMETYPE_MAP(XX)                                                                                               \
	XX(invalid, "Invalid")                                                                                             \
	XX(absolute, "Absolute time, given in seconds")                                                                    \
	XX(now, "The current date/time")                                                                                   \
	XX(sunrise, "Dawn")                                                                                                \
	XX(sunset, "Dusk")                                                                                                 \
	XX(boot, "Boot time")

// Type of absolute timer
enum class TimeType {
#define XX(type, comment) type,
	TIMETYPE_MAP(XX)
#undef XX
};

String toString(TimeType type);
TimeType decodeTimeStr(const char* str, int& seconds);

class TimeManager
{
public:
	void onChange(TimeChangeDelegate callback)
	{
		changeCallback = callback;
	}

	void configure(JsonObjectConst config);
	void update(time_t timeUTC);

	/**
	 * @brief Obtain a standard time value in local time
	 * @param tt Kind of time to get
	 * @param offset_secs Adjustment to value required
	 * @retval time_t Local time
	 */
	time_t getTime(TimeType tt, int offset_secs = 0);

	String timeStr(time_t t);

	String timeStr(TimeType tt, int offset_secs = 0)
	{
		return timeStr(getTime(tt, offset_secs));
	}

	time_t getBootTimeUTC() const
	{
		return bootTimeUTC;
	}

	Timezone zone;

private:
	time_t bootTimeUTC = 0;
	//
	SolarCalculator solarCalc;
	//
	TimeChangeDelegate changeCallback = nullptr;
};

extern TimeManager timeManager;
