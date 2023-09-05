#ifndef CR_WindDir_h
#define CR_WindDir_h
#include "Arduino.h"

struct WindVane
{
	float degree;
	int valueMin;
	int valueMax;
};

struct WindDirection
{
	double degree;
	const char *name;
};

class WindDir
{
public:
	WindDir(int pin, short indexOffset);

	void begin();
	void loop(int minute, int second);
	WindDirection getWindDir(int timePeriodInMinutes);

private:
	short getWindDirIndex(int rawValue);
	void getWindForMinutes(int minutes);
	void calcAvg(short currentIndex);
	double roundValue(float value);

	WindVane _windVane[17] = {
		{-1.0, 0, 0},
		{0.0, 2921, 3163},
		{22.5, 1472, 1626},
		{45.0, 1682, 1859},
		{67.5, 273, 296},
		{90.0, 301, 333},
		{112.5, 204, 221},
		{135.0, 635, 701},
		{157.5, 421, 465},
		{180.0, 1016, 1122},
		{202.5, 854, 944},
		{225.0, 2368, 2550},
		{247.5, 2195, 2367},
		{270.0, 3745, 4132},
		{292.5, 3164, 3408},
		{315.0, 3409, 3744},
		{337.5, 2585, 2857},
	};

	int _pin;
	int _second;
	int _minute;
	short _indexOffset;

	float _avgX1M;
	float _avgY1M;
	float _avgX10M;
	float _avgY10M;

	float _currentDegree;
	float _avg1M;
	float _avg10M;
};

#endif