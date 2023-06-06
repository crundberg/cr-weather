#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

// Wifi
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT
IPAddress mqttServer(192, 168, 50, 200);
const char *mqttUser = MQTT_USER;
const char *mqttPassword = MQTT_PASSWORD;
unsigned long mqttLastReconnectAttempt = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// One Wire
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// Wind speed
byte windSpeedCnt = 0;
float windSpeedAvg1M = 0.0;
float windSpeedAvg10M = 0.0;
float windSpeedAvg1H = 0.0;
unsigned long lastWindSpeedTime = 0;
float windSpeedPerPulse = 2.4 * 1000 / 3600; // 2.4 km/h per pulse converted to m/s

// Rain gauge
long rainCnt = 0;
byte rainPerMinute[60];
int rainPerHour[24];
long rain10M = 0;
long rain1H = 0;
long rainToday = 0;
long rainYesterday = 0;
const float rainPerPulse = 0.2794; // 0.2794 mm per pulse
unsigned long lastRainTime = 0;

// Time
unsigned long lastTime = 0;
byte second = 0;
byte minute = 0;
byte hour = 0;
byte day = 0;

// Led
bool toggleLed = false;

// Wind direction
struct WindDir
{
	const char name[4];
	float degree;
	int valueMin;
	int valueMax;
};

int windDirectionIdx = 0;
float windDirectionAvg = 0;

WindDir windDirections[17] = {
	{"N/A", -1.0, 0, 0},
	{"ESE", 112.5, 63, 69},
	{"NEE", 67.5, 80, 88},
	{"E", 90.0, 88, 98},
	{"SES", 157.5, 120, 133},
	{"SE", 135.0, 175, 194},
	{"SSW", 202.5, 232, 257},
	{"S", 180.0, 273, 301},
	{"NNE", 22.5, 385, 426},
	{"NE", 45.0, 438, 484},
	{"SWW", 247.5, 569, 614}, // Overlapping, changed 629 to 614
	{"SW", 225.0, 614, 661},  // Overlapping, changed 598 to 614
	{"NWN", 337.5, 667, 737},
	{"N", 0.0, 746, 804},	  // Overlapping, changed 824 to 804
	{"WNW", 292.5, 804, 855}, // Overlapping, changed 785 to 804 and 868 to 855
	{"NW", 315.0, 855, 914},  // Overlapping, changed 842 to 855 and 931 to 914
	{"W", 270.0, 914, 992},	  // Overlapping, changed 897 to 914
};

void ICACHE_RAM_ATTR windSpeedIsr()
{
	unsigned long now = millis();

	if (now - lastWindSpeedTime > 250)
	{
		windSpeedCnt += 1;
		lastWindSpeedTime = now;

		Serial.print("Wind speed pulse: ");
		Serial.println(windSpeedCnt);
	}
}

void ICACHE_RAM_ATTR rainIsr()
{
	unsigned long now = millis();

	if (now - lastRainTime > 250)
	{
		rainCnt += 1;
		lastRainTime = now;

		Serial.print("Rain pulse: ");
		Serial.println(rainCnt);
	}
}

void setup()
{
	Serial.begin(9600);

	setupWifi();
	setupMqtt();

	pinMode(BUILTIN_LED, OUTPUT);
	pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
	pinMode(RAIN_PIN, INPUT_PULLUP);

	attachInterrupt(WIND_SPEED_PIN, windSpeedIsr, FALLING);
	attachInterrupt(RAIN_PIN, rainIsr, FALLING);
}

void setupWifi()
{
	Serial.println();
	Serial.println();
	Serial.print("Connecting to ");
	Serial.print(ssid);

	WiFi.hostname("CR-Weather");
	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		Serial.print(".");
	}

	Serial.println("");
	Serial.print("Connected with IP ");
	Serial.println(WiFi.localIP());
	Serial.println("");
}

void setupMqtt()
{
	client.setServer(mqttServer, 1883);
	// client.setCallback(callback);
}

void loop()
{
	bool newSecond = generateTime();

	everySecond(newSecond);
	everyTenSeconds(newSecond);
	everyMinute(newSecond);
	everyDay(newSecond);

	mqttLoop();
}

boolean mqttConnect()
{
	Serial.println("Connecting to MQTT...");

	if (client.connect("ArduinoWeather", mqttUser, mqttPassword))
	{
		Serial.println("Connected to MQTT!");
		// Once connected, publish an announcement...
		// client.publish("outTopic", "hello world");
		// ... and resubscribe
		// client.subscribe("inTopic");
	}
	else
	{
		Serial.print("Connection to MQTT failed, reason code: ");
		Serial.print(client.state());
		Serial.println(" try again in 5 seconds");
	}
	return client.connected();
}

void mqttLoop()
{
	if (!client.connected())
	{
		long now = millis();

		if (now - mqttLastReconnectAttempt > 5000)
		{
			mqttLastReconnectAttempt = now;

			if (mqttConnect())
			{
				mqttLastReconnectAttempt = 0;
			}
		}
	}
	else
	{
		// Client connected
		client.loop();
	}
}

void everySecond(bool newSecond)
{
	if (!newSecond)
		return;

	// Toggle blue led
	toggleLed = !toggleLed;
	digitalWrite(LED_BUILTIN, toggleLed);

	// Wind direction
	windDirection();

	// Wind speed
	windSpeed();

	// Rain
	rain();
}

void everyTenSeconds(bool newSecond)
{
	if (!newSecond)
		return;

	if (second % 10 > 0)
		return;

	Serial.println("Every ten seconds event");

	// Publish wind direction to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["name"] = windDirections[windDirectionIdx].name;
	jsonDoc["degree"] = windDirections[windDirectionIdx].degree;
	jsonDoc["degreeAvg1M"] = windDirectionAvg;
	mqttPublish("weather/wind/direction", jsonDoc, true);
	jsonDoc.clear();

	// Publish wind speed to MQTT
	jsonDoc["avg1M"] = windSpeedAvg1M * windSpeedPerPulse;
	jsonDoc["avg10M"] = windSpeedAvg10M * windSpeedPerPulse;
	jsonDoc["avg1H"] = windSpeedAvg1H * windSpeedPerPulse;
	mqttPublish("weather/wind/speed", jsonDoc, true);
}

void mqttPublish(char *topic, StaticJsonDocument<200> jsonDocument, bool retained)
{
	char payload[MQTT_BUFFER_SIZE];
	serializeJson(jsonDocument, payload);

	// Publish to MQTT
	Serial.print("MQTT - Publish message: ");
	Serial.println(payload);
	client.publish(topic, payload, retained);
}

void everyMinute(bool newSecond)
{
	if (!newSecond)
		return;

	if (second > 0)
		return;

	Serial.println("Every minute event");

	// Temperature
	temperature();

	// Sum rain for time period
	sumRainForTimePeriod();

	// Publish rain to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["last10M"] = rain10M * rainPerPulse;
	jsonDoc["last1H"] = rain1H * rainPerPulse;
	jsonDoc["today"] = rainToday * rainPerPulse;
	jsonDoc["yesterday"] = rainYesterday * rainPerPulse;
	mqttPublish("weather/rain", jsonDoc, true);
}

void everyDay(bool newSecond)
{
	if (!newSecond)
		return;

	if (hour > 0 || minute > 0 || second > 0)
		return;

	Serial.println("Every day event");

	// Sum rain for time period
	rainYesterday = rainToday;
	rainToday = 0;
}

bool generateTime()
{
	unsigned long currentTime = millis();

	unsigned long timeDelta = currentTime - lastTime;
	bool newSecond = timeDelta > 1000;

	if (newSecond)
	{
		second += 1;

		if (second >= 60)
		{
			second = 0;
			minute += 1;
		}

		if (minute >= 60)
		{
			minute = 0;
			hour += 1;
		}

		if (hour >= 24)
		{
			hour = 0;
			day += 1;
		}

		// Round the latest time to the nearest second
		lastTime = currentTime - (timeDelta - 1000);
	}

	return newSecond;
}

void temperature()
{
	// Send the command to get temperatures
	sensors.requestTemperatures();

	// Publish temperature to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["value"] = sensors.getTempCByIndex(0);
	mqttPublish("weather/temp", jsonDoc, true);
}

void rain()
{
	rainPerMinute[minute] += rainCnt;
	rainPerHour[hour] += rainCnt;
	rainToday += rainCnt;
	rainCnt = 0;
}

void sumRainForTimePeriod()
{
	int timePeriod = minute;
	rain10M = 0;
	rain1H = 0;

	for (int i = 0; i <= 59; i += 1)
	{
		timePeriod -= 1;

		if (timePeriod < 0)
			timePeriod = 59;

		if (i < 10)
		{
			rain10M += rainPerMinute[timePeriod];
		}

		rain1H += rainPerMinute[timePeriod];
	}
}

void windSpeed()
{
	windSpeedAvg1M = calcAverage(windSpeedAvg1M, windSpeedCnt, 60);
	windSpeedAvg10M = calcAverage(windSpeedAvg10M, windSpeedCnt, 600);
	windSpeedAvg1H = calcAverage(windSpeedAvg1H, windSpeedCnt, 600);
	windSpeedCnt = 0;
}

void windDirection()
{
	// Wemos D1 mini has 10 bits precision, 0-1023
	int rawValue = analogRead(WIND_DIRECTION_PIN);

	// Loop through all direction to find current direction
	for (int i = 1; i <= 16; i += 1)
	{
		if (rawValue >= windDirections[i].valueMin && rawValue <= windDirections[i].valueMax)
		{
			windDirectionIdx = i;
			windDirectionAvg = calcAverage(windDirectionAvg, windDirections[i].degree, 60);
			break;
		}
	}

	// Print warning if non was found
	/*
	if (windDirectionIdx <= 0)
	{
		Serial.print("Warning! Wind direction not found! ");
		Serial.print("(Value=");
		Serial.print(rawValue);
		Serial.println(")");
	}
	*/
}

float calcAverage(float lastAvg, float newValue, int samples)
{
	return (((lastAvg * (samples - 1)) + newValue) / samples);
}
