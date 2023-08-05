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
		{0.0, 791, 853},
		{22.5, 423, 467},
		{45.0, 476, 526},
		{67.5, 91, 100},
		{90.0, 100, 111},
		{112.5, 73, 81},
		{135.0, 197, 217},
		{157.5, 137, 151},
		{180.0, 300, 332},
		{202.5, 257, 284},
		{225.0, 657, 708},
		{247.5, 610, 657},
		{270.0, 965, 1048},
		{292.5, 854, 904},
		{315.0, 904, 965},
		{337.5, 712, 787},
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