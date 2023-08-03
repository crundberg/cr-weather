#include "Arduino.h"
#include "rain.h"
#include "time.h"

#define RAIN_PER_TICK 0.2794
#define NO_OF_DECIMALS 2

Rain::Rain(int pin)
{
	pinMode(pin, INPUT_PULLUP);

	_lastTick = 0;
}

void Rain::count()
{
	// Serial.println("[Rain] Count");
	unsigned long now = millis();

	if (now - _lastTick > 15)
	{
		_ticks += 1;
		_lastTick = now;

		// Serial.print("[Rain] Ticks: ");
		// Serial.println(_ticks);
	}
}

void Rain::loop(int hour, int minute)
{
	// Return if not a new minute
	if (minute == _minute)
		return;

	// Save yesterdays rain
	if (hour == 0 && minute == 0)
	{
		_ticksYesterday = _ticksToday;
		_ticksToday = 0;
	}

	// Reset ticks on new minute
	if (minute != _minute)
		_ticksPerMin[minute] = 0;

	// Reset ticks on new hour
	if (hour != _hour)
		_ticksPerHour[hour] = 0;

	_hour = hour;
	_minute = minute;

	// Increase ticks for current minute and hour
	_ticksPerMin[minute] += _ticks;
	_ticksPerHour[hour] += _ticks;
	_ticksToday += _ticks;

	// Save data
	_ticks = 0;
}

double Rain::getRainForLastMinutes(int timePeriodInMinutes)
{
	// Adjust minimum and maximum value for time period
	if (timePeriodInMinutes < 1)
		timePeriodInMinutes = 1;
	if (timePeriodInMinutes > 60)
		timePeriodInMinutes = 60;

	// Set initial values
	int timePeriod = _minute;
	int ticksForTimePeriod = 0;

	// Loop through selected time period
	for (int index = 0; index < timePeriodInMinutes; index += 1)
	{
		// Decrease time period with 1 minute
		timePeriod -= 1;

		// Adjust minute if less than 0
		if (timePeriod < 0)
			timePeriod = 59;

		// Add ticks for minute to ticks for time period
		ticksForTimePeriod += _ticksPerMin[timePeriod];
	}

	// Convert ticks to rain during time period
	return roundValue(ticksForTimePeriod * RAIN_PER_TICK);
}

double Rain::getRainForLastHours(int timePeriodInHours)
{
	// Adjust minimum and maximum value for time period
	if (timePeriodInHours < 1)
		timePeriodInHours = 1;
	if (timePeriodInHours > 23)
		timePeriodInHours = 23;

	// Set initial values
	int timePeriod = _hour;
	int ticksForTimePeriod = 0;

	// Loop through selected time period
	for (int index = 0; index < timePeriodInHours; index += 1)
	{
		// Add ticks for hour to ticks for time period
		ticksForTimePeriod += _ticksPerHour[timePeriod];

		// Decrease time period with 1 hour
		timePeriod -= 1;

		// Adjust hour if less than 0
		if (timePeriod < 0)
			timePeriod = 23;
	}

	// Convert ticks to rain during time period
	return roundValue(ticksForTimePeriod * RAIN_PER_TICK);
}

double Rain::getRainForToday()
{
	return roundValue(_ticksToday * RAIN_PER_TICK);
}

double Rain::getRainForYesterday()
{
	return roundValue(_ticksYesterday * RAIN_PER_TICK);
}

double Rain::roundValue(float value)
{
	double decimals = pow(10, NO_OF_DECIMALS);
	return (int)(value * decimals + 0.5) / decimals;
}