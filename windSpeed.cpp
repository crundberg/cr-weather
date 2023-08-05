#include "Arduino.h"
#include "windSpeed.h"
#include "time.h"

#define WIND_SPEED_PER_TICK 2.4 * 1000 / 3600; // 2.4 km/h per pulse converted to m/s
#define NO_OF_DECIMALS 2

WindSpeed::WindSpeed(int pin)
{
	_pin = pin;
	_lastTick = 0;
}

void WindSpeed::begin()
{
	pinMode(_pin, INPUT_PULLUP);
}

void WindSpeed::count()
{
	// Serial.println("[WindSpeed] Count");
	unsigned long now = millis();

	if (now - _lastTick > 15)
	{
		_ticks += 1;
		_lastTick = now;

		// Serial.print("[WindSpeed] Ticks: ");
		// Serial.println(_ticks);
	}
}

void WindSpeed::loop(int minute, int second)
{
	// Return if not a new second
	if (second == _second)
		return;

	if (minute != _minute)
		_ticksPerMin[minute] = 0;

	_second = second;
	_minute = minute;

	// Increase ticks for current minute
	_ticksPerMin[minute] += _ticks;

	// Save highest gust for every minute
	short gustLast2s = _ticksLastSec + _ticks;
	if (gustLast2s > _gustPerMin[minute])
		_gustPerMin[minute] = gustLast2s;

	// Save data
	_ticksLastSec = _ticks;
	_ticks = 0;
}

double WindSpeed::getWindSpeed(int timePeriodInMinutes)
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

	// Serial.print("[WindSpeed] Ticks=");
	// Serial.print(ticksForTimePeriod);
	// Serial.print(", timePeriodInMinutes=");
	// Serial.println(timePeriodInMinutes);

	// Convert ticks to average wind speed during time period
	float result = (float)ticksForTimePeriod / (float)timePeriodInMinutes / 60.0 * WIND_SPEED_PER_TICK;
	return roundValue(result);
}

double WindSpeed::getGust()
{
	// Reset ticks for gust
	int gustTicks = 0;

	// Loop through gust for last 60 minutes
	for (int i = 0; i < 60; i += 1)
	{
		// Save highest value
		if (gustTicks < _gustPerMin[_minute])
			gustTicks = _gustPerMin[_minute];
	}

	// Serial.print("[WindSpeed] gustTicks=");
	// Serial.println(gustTicks);

	// Convert ticks for gust to wind speed
	float result = (float)gustTicks / 2.0 * WIND_SPEED_PER_TICK;
	return roundValue(result);
}

short WindSpeed::getTicksLastSec()
{
	return _ticksLastSec;
}

double WindSpeed::roundValue(float value)
{
	double decimals = pow(10, NO_OF_DECIMALS);
	return (int)(value * decimals + 0.5) / decimals;
}