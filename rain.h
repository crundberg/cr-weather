#ifndef CR_Rain_h
#define CR_Rain_h
#include "Arduino.h"

class Rain
{
public:
	Rain(int pin);

	void loop(int hour, int minute);

	void begin();
	double getRainForLastMinutes(int lastNoMinutes);
	double getRainForLastHours(int lastNoHours);
	double getRainForToday();
	double getRainForYesterday();
	unsigned long getTotaltTicks();

	void count();

private:
	double roundValue(float value);

	int _pin;

	short _ticks;
	unsigned long _lastTick;

	unsigned short _ticksPerMin[60];
	unsigned int _ticksPerHour[24];

	int _ticksToday;
	int _ticksYesterday;
	unsigned long _totaltTicks;

	int _hour;
	int _minute;
};

#endif