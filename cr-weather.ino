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

	if (now - lastWindSpeedTime > 50)
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

	if (now - lastRainTime > 50)
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
}

void setupOTA()
{
	// OTA
	ArduinoOTA.setPort(8266);
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
	windDirectionIdx = 0;

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
