#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "time.h"
#include "config.h"

// Wifi
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT
IPAddress mqttServer(192, 168, 50, 200);
const char *mqttUser = MQTT_USER;
const char *mqttPassword = MQTT_PASSWORD;
const char *mqttRainTopic = "weather/rain";
const char *mqttWindSpeedTopic = "weather/wind/speed";
const char *mqttWindDirTopic = "weather/wind/direction";
const char *mqttTempTopic = "weather/temp";
unsigned long mqttLastReconnectAttempt = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// One Wire
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// Time
const long gmtOffsetInSeconds = 3600;
const int daylightOffsetInSeconds = 3600;

// Wind speed
volatile byte windSpeedCnt = 0;
volatile unsigned long lastWindSpeedTime = 0;
int windSpeedPerMinute[60];
byte windSpeedCntLastSec = 0;
byte windGust = 0;
unsigned long windSpeed1M = 0;
unsigned long windSpeed10M = 0;
unsigned long windSpeed1H = 0;
byte highestGustPerMinute[60];
const float windSpeedPerPulse = 2.4 * 1000 / 3600; // 2.4 km/h per pulse converted to m/s

// Rain gauge
volatile byte rainCnt = 0;
volatile unsigned long lastRainTime = 0;
byte rainPerMinute[60];
int rainPerHour[24];
unsigned long rain10M = 0;
unsigned long rain1H = 0;
unsigned long rainToday = 0;
unsigned long rainYesterday = 0;
const float rainPerPulse = 0.2794; // 0.2794 mm per pulse

// Time
unsigned long lastTime = 0;
int second = 0;
int minute = 0;
int hour = 0;
int day = 0;
time_t rawTime;
struct tm *timeInfo;
int lastSecond = 0;

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
int windDirectionOffset = 6;

WindDir windDirections[17] = {
	{"N/A", -1.0, 0, 0},
	{"N", 0.0, 791, 853},
	{"NNE", 22.5, 423, 467},
	{"NE", 45.0, 476, 526},
	{"ENE", 67.5, 91, 100},
	{"E", 90.0, 100, 111},
	{"ESE", 112.5, 73, 81},
	{"SE", 135.0, 197, 217},
	{"SSE", 157.5, 137, 151},
	{"S", 180.0, 300, 332},
	{"SSW", 202.5, 257, 284},
	{"SW", 225.0, 657, 708},
	{"WSW", 247.5, 610, 657},
	{"W", 270.0, 965, 1048},
	{"WNW", 292.5, 854, 904},
	{"NW", 315.0, 904, 965},
	{"NNW", 337.5, 712, 787},
};

void ICACHE_RAM_ATTR windSpeedIsr()
{
	unsigned long now = millis();

	if (now - lastWindSpeedTime > 15)
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

	if (now - lastRainTime > 15)
	{
		rainCnt += 1;
		lastRainTime = now;

		Serial.print("Rain pulse: ");
		Serial.println(rainCnt);
	}
}

void setup()
{
	Serial.begin(115200);

	setupWifi();
	setupMqtt();
	setupOTA();
	setupTime();

	pinMode(BUILTIN_LED, OUTPUT);
	pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
	pinMode(RAIN_PIN, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), windSpeedIsr, FALLING);
	attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainIsr, FALLING);
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
}

void setupOTA()
{
	// OTA
	ArduinoOTA.setHostname("CR-Weather");

	ArduinoOTA.onStart([]()
					   {  
		String type;  
		if (ArduinoOTA.getCommand() == U_FLASH) {  
			type = "sketch";  
		} else { // U_SPIFFS  
			type = "filesystem";  
		}
   
		// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()  
		Serial.println("Start updating " + type); });

	ArduinoOTA.onEnd([]()
					 { Serial.println("\nEnd"); });
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
						  { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
	ArduinoOTA.onError([](ota_error_t error)
					   {  
   Serial.printf("Error[%u]: ", error);  
   if (error == OTA_AUTH_ERROR) {  
    Serial.println("Auth Failed");  
   } else if (error == OTA_BEGIN_ERROR) {  
    Serial.println("Begin Failed");  
   } else if (error == OTA_CONNECT_ERROR) {  
    Serial.println("Connect Failed");  
   } else if (error == OTA_RECEIVE_ERROR) {  
    Serial.println("Receive Failed");  
   } else if (error == OTA_END_ERROR) {  
    Serial.println("End Failed");  
   } });

	ArduinoOTA.begin();
}

void setupTime()
{
	Serial.println("Setting up time");

	// Init and get the time
	configTime(gmtOffsetInSeconds, daylightOffsetInSeconds, "pool.ntp.org");
	bool newSecond;
	int year = 0;

	while (year < 2023)
	{
		delay(1000);
		Serial.print(".");

		newSecond = updateTime();
		year = 1900 + timeInfo->tm_year;
	}

	Serial.println("");
	Serial.println("Time setup");
}

bool updateTime()
{
	time(&rawTime);
	timeInfo = localtime(&rawTime);

	day = timeInfo->tm_mday;
	hour = timeInfo->tm_hour;
	minute = timeInfo->tm_min;
	second = timeInfo->tm_sec;

	if (second != lastSecond)
	{
		lastSecond = second;
		return true;
	}

	return false;
}

void loop()
{
	ArduinoOTA.handle();

	bool newSecond = updateTime();
	everySecond(newSecond);
	everyTenSeconds(newSecond);
	everyMinute(newSecond);
	everyHour(newSecond);
	everyDay(newSecond);

	mqttLoop();
}

boolean mqttConnect()
{
	Serial.println("Connecting to MQTT...");

	if (client.connect("ArduinoWeather", mqttUser, mqttPassword))
	{
		Serial.println("Connected to MQTT!");
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
		unsigned long now = millis();

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

void mqttPublish(const char *topic, StaticJsonDocument<200> jsonDocument, bool retained)
{
	char payload[MQTT_BUFFER_SIZE];
	serializeJson(jsonDocument, payload);

	// Publish to MQTT
	Serial.print("MQTT - Publish message: ");
	Serial.println(payload);
	client.publish(topic, payload, retained);
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
	mqttPublish(mqttWindDirTopic, jsonDoc, true);
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
	rainPerMinute[minute] = 0;
	sumRainForTimePeriod();

	// Sum wind speed for time period
	windSpeedPerMinute[minute] = 0;
	sumWindSpeedForTimePeriod();

	// Calculate wind gust
	highestGustPerMinute[minute] = 0;
	windGust = 0;

	for (byte min = 0; min < 60; min += 1)
	{
		if (highestGustPerMinute[min] > windGust)
		{
			windGust = highestGustPerMinute[min];
		}
	}

	// Publish rain to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["last10M"] = rain10M * rainPerPulse;
	jsonDoc["last1H"] = rain1H * rainPerPulse;
	jsonDoc["today"] = rainToday * rainPerPulse;
	jsonDoc["yesterday"] = rainYesterday * rainPerPulse;
	mqttPublish(mqttRainTopic, jsonDoc, true);
	jsonDoc.clear();

	// Publish wind speed to MQTT
	jsonDoc["avg1M"] = windSpeed1M / 60 * windSpeedPerPulse;
	jsonDoc["avg10M"] = windSpeed10M / 600 * windSpeedPerPulse;
	jsonDoc["avg1H"] = windSpeed1H / 3600 * windSpeedPerPulse;
	jsonDoc["gust"] = windGust * windSpeedPerPulse;
	mqttPublish(mqttWindSpeedTopic, jsonDoc, true);
}

void everyHour(bool newSecond)
{
	if (!newSecond)
		return;

	if (minute > 0)
		return;

	if (second > 0)
		return;

	Serial.println("Every hour event");
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

void temperature()
{
	// Send the command to get temperatures
	sensors.requestTemperatures();

	// Publish temperature to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["value"] = sensors.getTempCByIndex(0);
	mqttPublish(mqttTempTopic, jsonDoc, true);
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
	windSpeedPerMinute[minute] += windSpeedCnt;

	byte windSpeedLast2S = windSpeedCntLastSec + windSpeedCnt;
	if (windSpeedLast2S > highestGustPerMinute[minute])
	{
		highestGustPerMinute[minute] = windSpeedLast2S;
	}

	windSpeedCntLastSec = windSpeedCnt;
	windSpeedCnt = 0;
}

void sumWindSpeedForTimePeriod()
{
	int timePeriod = minute;
	windSpeed1M = 0;
	windSpeed10M = 0;
	windSpeed1H = 0;

	for (int i = 0; i <= 59; i += 1)
	{
		timePeriod -= 1;

		if (timePeriod < 0)
			timePeriod = 59;

		if (i < 1)
			windSpeed1M = windSpeedPerMinute[timePeriod];

		if (i < 10)
			windSpeed10M += windSpeedPerMinute[timePeriod];

		windSpeed1H += windSpeedPerMinute[timePeriod];
	}
}

void windDirection()
{
	// Wemos D1 mini has 10 bits precision, 0-1023
	int rawValue = analogRead(WIND_DIRECTION_PIN);
	windDirectionIdx = 0;

	// Loop through all direction to find current direction
	for (int i = 1; i <= 16; i += 1)
	{
		if (rawValue >= windDirections[i].valueMin && rawValue <= windDirections[i].valueMax)
		{
			windDirectionIdx = i + windDirectionOffset;

			if (windDirectionIdx > 16)
				windDirectionIdx -= 16;
			if (windDirectionIdx < 1)
				windDirectionIdx += 16;

			windDirectionAvg = calcAverage(windDirectionAvg, windDirections[i].degree, 60);
			break;
		}
	}

	// Print warning if non was found
	if (windDirectionIdx <= 0)
	{
		Serial.print("Warning! Wind direction not found! ");
		Serial.print("(Value=");
		Serial.print(rawValue);
		Serial.println(")");
	}
}

float calcAverage(float lastAvg, float newValue, int samples)
{
	return (((lastAvg * (samples - 1)) + newValue) / samples);
}
