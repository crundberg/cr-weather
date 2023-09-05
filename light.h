#ifndef CR_Light_h
#define CR_Light_h
#include "Arduino.h"
#include "Wire.h"

class Light
{
public:
	Light(uint8_t addr);

	void begin(TwoWire *i2c);
	void loop(int minute, int second);
	uint32_t getLux();

private:
	void enable();
	void disable();
	uint32_t calcLux(uint16_t broadband, uint16_t ir);
	void i2cWrite8(uint8_t reg, uint8_t value);
	uint8_t i2cRead8(uint8_t reg);
	uint8_t i2cRead16(uint8_t reg);

	TwoWire *_i2c;
	int _addr;
	int _second;
	int _minute;

	uint32_t _lux;
};

#endif