#include "Arduino.h"
#include "light.h"
#include <Wire.h>

#define COMMAND_BIT (0x80)
#define REGISTER_CONTROL (0x00)
#define CONTROL_POWER_ON (0x03)
#define CONTROL_POWER_OFF (0x00)
#define WORD_BIT (0x20)
#define REGISTER_CH0_LOW (0x0C)
#define REGISTER_CH1_LOW (0x0E)
#define CLIPPING_13MS (4900)	   ///< # Counts that trigger a change in gain/integration
#define LUX_CHSCALE_TINT0 (0x7517) ///< 322/11 * 2^LUX_CHSCALE
#define LUX_LUXSCALE (14)		   ///< Scale by 2^14
#define LUX_RATIOSCALE (9)		   ///< Scale ratio by 2^9
#define LUX_CHSCALE (10)		   ///< Scale channel values by 2^10

// T, FN and CL package values
#define LUX_K1T (0x0040) ///< 0.125 * 2^RATIO_SCALE
#define LUX_B1T (0x01f2) ///< 0.0304 * 2^LUX_SCALE
#define LUX_M1T (0x01be) ///< 0.0272 * 2^LUX_SCALE
#define LUX_K2T (0x0080) ///< 0.250 * 2^RATIO_SCALE
#define LUX_B2T (0x0214) ///< 0.0325 * 2^LUX_SCALE
#define LUX_M2T (0x02d1) ///< 0.0440 * 2^LUX_SCALE
#define LUX_K3T (0x00c0) ///< 0.375 * 2^RATIO_SCALE
#define LUX_B3T (0x023f) ///< 0.0351 * 2^LUX_SCALE
#define LUX_M3T (0x037b) ///< 0.0544 * 2^LUX_SCALE
#define LUX_K4T (0x0100) ///< 0.50 * 2^RATIO_SCALE
#define LUX_B4T (0x0270) ///< 0.0381 * 2^LUX_SCALE
#define LUX_M4T (0x03fe) ///< 0.0624 * 2^LUX_SCALE
#define LUX_K5T (0x0138) ///< 0.61 * 2^RATIO_SCALE
#define LUX_B5T (0x016f) ///< 0.0224 * 2^LUX_SCALE
#define LUX_M5T (0x01fc) ///< 0.0310 * 2^LUX_SCALE
#define LUX_K6T (0x019a) ///< 0.80 * 2^RATIO_SCALE
#define LUX_B6T (0x00d2) ///< 0.0128 * 2^LUX_SCALE
#define LUX_M6T (0x00fb) ///< 0.0153 * 2^LUX_SCALE
#define LUX_K7T (0x029a) ///< 1.3 * 2^RATIO_SCALE
#define LUX_B7T (0x0018) ///< 0.00146 * 2^LUX_SCALE
#define LUX_M7T (0x0012) ///< 0.00112 * 2^LUX_SCALE
#define LUX_K8T (0x029a) ///< 1.3 * 2^RATIO_SCALE
#define LUX_B8T (0x0000) ///< 0.000 * 2^LUX_SCALE
#define LUX_M8T (0x0000) ///< 0.000 * 2^LUX_SCALE

Light::Light(uint8_t addr)
{
	_addr = addr;
}

void Light::begin(TwoWire *i2c)
{
	_i2c = i2c;

	enable();
}

void Light::loop(int minute, int second)
{
	// Power on
	if (second == 50 && second != _second)
		enable();

	_second = second;

	// Return if not a new minute
	if (minute == _minute)
		return;

	_minute = minute;

	// Read value from channel 0 (Visible and infrared) */
	uint16_t broadband = i2cRead16(COMMAND_BIT | WORD_BIT | REGISTER_CH0_LOW);

	// Read value from channel 1 (Infrared) */
	uint16_t ir = i2cRead16(COMMAND_BIT | WORD_BIT | REGISTER_CH1_LOW);

	// Calculate lux
	_lux = calcLux(broadband, ir);

	disable();
}

uint32_t Light::getLux()
{
	return _lux;
}

void Light::enable()
{
	i2cWrite8(COMMAND_BIT | REGISTER_CONTROL, CONTROL_POWER_ON);
}

void Light::disable()
{
	i2cWrite8(COMMAND_BIT | REGISTER_CONTROL, CONTROL_POWER_OFF);
}

uint32_t Light::calcLux(uint16_t broadband, uint16_t ir)
{
	unsigned long chScale;
	unsigned long channel1;
	unsigned long channel0;

	/* Make sure the sensor isn't saturated! */
	uint16_t clipThreshold = CLIPPING_13MS;

	/* Return 65536 lux if the sensor is saturated */
	if ((broadband > clipThreshold) || (ir > clipThreshold))
	{
		return 65536;
	}

	/* Get the correct scale depending on the intergration time */
	chScale = LUX_CHSCALE_TINT0;

	/* Scale for gain (1x or 16x) */
	chScale = chScale << 4;

	/* Scale the channel values */
	channel0 = (broadband * chScale) >> LUX_CHSCALE;
	channel1 = (ir * chScale) >> LUX_CHSCALE;

	/* Find the ratio of the channel values (Channel1/Channel0) */
	unsigned long ratio1 = 0;
	if (channel0 != 0)
		ratio1 = (channel1 << (LUX_RATIOSCALE + 1)) / channel0;

	/* round the ratio value */
	unsigned long ratio = (ratio1 + 1) >> 1;

	unsigned int b, m;

	if ((ratio >= 0) && (ratio <= LUX_K1T))
	{
		b = LUX_B1T;
		m = LUX_M1T;
	}
	else if (ratio <= LUX_K2T)
	{
		b = LUX_B2T;
		m = LUX_M2T;
	}
	else if (ratio <= LUX_K3T)
	{
		b = LUX_B3T;
		m = LUX_M3T;
	}
	else if (ratio <= LUX_K4T)
	{
		b = LUX_B4T;
		m = LUX_M4T;
	}
	else if (ratio <= LUX_K5T)
	{
		b = LUX_B5T;
		m = LUX_M5T;
	}
	else if (ratio <= LUX_K6T)
	{
		b = LUX_B6T;
		m = LUX_M6T;
	}
	else if (ratio <= LUX_K7T)
	{
		b = LUX_B7T;
		m = LUX_M7T;
	}
	else if (ratio > LUX_K8T)
	{
		b = LUX_B8T;
		m = LUX_M8T;
	}

	unsigned long temp;
	channel0 = channel0 * b;
	channel1 = channel1 * m;

	temp = 0;
	/* Do not allow negative lux value */
	if (channel0 > channel1)
		temp = channel0 - channel1;

	/* Round lsb (2^(LUX_SCALE-1)) */
	temp += (1 << (LUX_LUXSCALE - 1));

	/* Strip off fractional portion */
	uint32_t lux = temp >> LUX_LUXSCALE;

	/* Signal I2C had no errors */
	return lux;
}

void Light::i2cWrite8(uint8_t reg, uint8_t value)
{
	_i2c->beginTransmission(_addr);
	_i2c->write(reg);
	_i2c->write(value);
	_i2c->endTransmission();

#ifdef I2C_DEBUG
	Serial.print("[I2C] Write 8: ");
	Serial.print(_addr, HEX);
	Serial.print(", ");
	Serial.print(reg, HEX);
	Serial.print(", ");
	Serial.println(value);
#endif
}

uint8_t Light::i2cRead8(uint8_t reg)
{
	_i2c->beginTransmission(_addr);
	_i2c->write(reg);
	_i2c->endTransmission();

	_i2c->requestFrom(_addr, 1);
	uint8_t value = _i2c->read();

#ifdef I2C_DEBUG
	Serial.print("[I2C] Read 8: ");
	Serial.print(_addr, HEX);
	Serial.print(", ");
	Serial.print(reg, HEX);
	Serial.print(", ");
	Serial.print(value, DEC);
	Serial.print(", ");
	Serial.println(value, BIN);
#endif

	return value;
}

uint8_t Light::i2cRead16(uint8_t reg)
{
	uint16_t value;

	_i2c->beginTransmission(_addr);
	_i2c->write(reg);
	_i2c->endTransmission();

	_i2c->requestFrom(_addr, 2);
	value = _i2c->read() << 8;
	value |= _i2c->read();

#ifdef I2C_DEBUG
	Serial.print("[I2C] Read 16: ");
	Serial.print(_addr, HEX);
	Serial.print(", ");
	Serial.print(reg, HEX);
	Serial.print(", ");
	Serial.print(value, DEC);
	Serial.print(", ");
	Serial.println(value, BIN);
#endif

	return value;
}