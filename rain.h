#ifndef CR_Rain_h
#define CR_Rain_h
#include "Arduino.h"

class Rain
{
public:
	Rain(int pin);

	void loop(int hour, int minute);
	double getRainForLastMinutes(int lastNoMinutes);
	double getRainForLastHours(int lastNoHours);
	double getRainForToday();
	double getRainForYesterday();

	void count();

private:
	double roundValue(float value);

	short _ticks;
	unsigned long _lastTick;

	unsigned short _ticksPerMin[60];
	unsigned int _ticksPerHour[24];

	double _rainYesterday;

	int _hour;
	int _minute;
};

#endif