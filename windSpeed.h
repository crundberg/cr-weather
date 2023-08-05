#ifndef CR_WindSpeed_h
#define CR_WindSpeed_h
#include "Arduino.h"

class WindSpeed
{
public:
	WindSpeed(int pin);

	void begin();
	void loop(int minute, int second);
	double getWindSpeed(int lastNoMinutes);
	double getGust();
	short getTicksLastSec();

	void count();

private:
	double roundValue(float value);

	int _pin;
	
	short _ticks;
	unsigned long _lastTick;
	short _ticksLastSec;

	int _ticksPerMin[60];
	short _gustPerMin[60];

	int _second;
	int _minute;
};

#endif