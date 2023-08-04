#include "Arduino.h"
#include "windDir.h"
#include "time.h"
#include "math.h"

#define NO_OF_DECIMALS 0

WindDir::WindDir(int pin, short indexOffset)
{
	_pin = pin;
	_indexOffset = indexOffset;
}

short WindDir::getWindDirIndex(int rawValue)
{
	short index = 0;

	for (int i = 1; i <= 16; i += 1)
	{
		if (rawValue >= _windVane[i].valueMin && rawValue <= _windVane[i].valueMax)
		{
			index = i + _indexOffset;

			if (index > 16)
				index -= 16;
			if (index < 1)
				index += 16;

			break;
		}
	}

	return index;
}

WindDirection WindDir::getWindDir(int timePeriodInMinutes)
{
	float value = _avg10M;

	if (timePeriodInMinutes <= 0)
		value = _currentDegree;

	if (timePeriodInMinutes == 1)
		value = _avg1M;

	int index = int((value / 22.5) + .5) % 16;
	const char *names[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

	WindDirection result = {roundValue(value), names[index]};
	return result;
}

void WindDir::loop(int minute, int second)
{
	// Return if not a new second
	if (second == _second)
		return;

	_second = second;
	_minute = minute;

	int rawValue = analogRead(_pin);
	short currentIndex = getWindDirIndex(rawValue);

	if (currentIndex < 0)
	{
		Serial.print("[WindDir] Wind direction not found! ");
		Serial.print("(Value=");
		Serial.print(rawValue);
		Serial.println(")");
		return;
	}

	calcAvg(currentIndex);
}

void WindDir::calcAvg(short currentIndex)
{
	float samples;

	// Get current degree
	_currentDegree = _windVane[currentIndex].degree;

	// Convert degree to radians
	float theta = _currentDegree / 180.0 * PI;

	// Running average for 1 minute
	samples = 0.25;
	_avgX1M = _avgX1M * (1 - samples) + cos(theta) * samples;
	_avgY1M = _avgY1M * (1 - samples) + sin(theta) * samples;

	// Running average for 10 minute
	samples = 0.1;
	_avgX10M = _avgX10M * (1 - samples) + cos(theta) * samples;
	_avgY10M = _avgY10M * (1 - samples) + sin(theta) * samples;

	// Convert result to degree
	_avg1M = atan2(_avgY1M, _avgX1M) / PI * 180;
	_avg10M = atan2(_avgY10M, _avgX10M) / PI * 180;

	// Result is -180 to 180. Adjust to 0-360.
	if (_avg1M < 0)
		_avg1M += 360;

	if (_avg10M < 0)
		_avg10M += 360;

	/*
	Serial.print("[WindDir] Current degree=");
	Serial.print(_currentDegree);
	Serial.print(", ");
	Serial.print(_avgX10M);
	Serial.print(", ");
	Serial.print(_avgY10M);
	Serial.print(", ");
	Serial.println(_avg10M);
	*/
}

double WindDir::roundValue(float value)
{
	double decimals = pow(10, NO_OF_DECIMALS);
	return (int)(value * decimals + 0.5) / decimals;
}