#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "time.h"
#include "config.h"
#include "windSpeed.h"
#include "windDir.h"
#include "rain.h"

// Wifi
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT
IPAddress mqttServer MQTT_SERVER_IP;
const char *mqttUser = MQTT_USER;
const char *mqttPassword = MQTT_PASSWORD;
const char *mqttRainTopic = "weather/rain";
const char *mqttWindSpeedTopic = "weather/wind/speed";
const char *mqttWindDirTopic = "weather/wind/direction";
const char *mqttTempTopic = "weather/temp";
const char *mqttUptimeTopic = "weather/uptime";
unsigned long mqttLastReconnectAttempt = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// One Wire
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

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

// Weather meter
WindSpeed windSpeed(WIND_SPEED_PIN);
WindDir windDir(WIND_DIRECTION_PIN, WIND_DIRECTION_OFFSET);
Rain rain(RAIN_PIN);

void setup()
{
	Serial.begin(115200);
	Serial.println();
	Serial.println();

	setupWifi();
	setupMqtt();
	// setupOTA();
	setupTime();

	pinMode(BUILTIN_LED, OUTPUT);
	attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), windSpeedIsr, FALLING);
	attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainIsr, FALLING);
}

IRAM_ATTR void windSpeedIsr()
{
	windSpeed.count();
}

IRAM_ATTR void rainIsr()
{
	rain.count();
}

void setupWifi()
{
	Serial.println("[WiFi] Setup");
	Serial.print("[WiFi] Connecting to ");
	Serial.print(ssid);

	WiFi.hostname("CR-Weather");
	WiFi.begin(ssid, password);
	WiFi.setAutoReconnect(true);
	WiFi.persistent(true);

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		Serial.print(".");
	}

	Serial.println("");
	Serial.print("[WiFi] Connected with IP ");
	Serial.println(WiFi.localIP());
}

void setupMqtt()
{
	client.setServer(mqttServer, 1883);
}

/*
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
*/

void setupTime()
{
	Serial.print("[Time] Setup");

	// Init and get the time
	configTime(GMT_OFFSET_IN_SECONDS, DAYLIGHT_OFFSET_IN_SECONDS, "pool.ntp.org");
	bool newSecond;
	int year = 0;

	while (year < 2023)
	{
		delay(500);
		Serial.print(".");

		newSecond = updateTime();
		year = 1900 + timeInfo->tm_year;
	}

	Serial.println("");
	Serial.println("[Time] Time is setup");
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
	// ArduinoOTA.handle();

	bool newSecond = updateTime();

	windSpeed.loop(minute, second);
	windDir.loop(minute, second);
	rain.loop(hour, minute);

	everySecond(newSecond);
	everyMinute(newSecond);

	mqttLoop();
}

boolean mqttConnect()
{
	Serial.println("[MQTT] Connect");

	if (client.connect("ArduinoWeather", mqttUser, mqttPassword))
	{
		Serial.println("[MQTT] Connected");
	}
	else
	{
		Serial.print("[MQTT] Connection failed with reason code: ");
		Serial.print(client.state());
		Serial.println(", retry in 5 seconds");
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
}

void mqttPublish(const char *topic, StaticJsonDocument<200> jsonDocument, bool retained)
{
	char payload[MQTT_BUFFER_SIZE];
	serializeJson(jsonDocument, payload);

	// Publish to MQTT
	Serial.print("[MQTT] Publish ");
	Serial.print(topic);
	Serial.print(": ");
	Serial.println(payload);
	client.publish(topic, payload, retained);
}

void everyMinute(bool newSecond)
{
	if (!newSecond)
		return;

	if (second > 0)
		return;

	Serial.println("[Event] Every minute event");

	// Temperature
	temperature();

	// Publish rain to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["last10M"] = rain.getRainForLastMinutes(10);
	jsonDoc["last1H"] = rain.getRainForLastMinutes(60);
	jsonDoc["today"] = rain.getRainForToday();
	jsonDoc["yesterday"] = rain.getRainForYesterday();
	mqttPublish(mqttRainTopic, jsonDoc, true);
	jsonDoc.clear();

	// Publish wind speed to MQTT
	jsonDoc["avg1M"] = windSpeed.getWindSpeed(1);
	jsonDoc["avg10M"] = windSpeed.getWindSpeed(10);
	jsonDoc["avg1H"] = windSpeed.getWindSpeed(60);
	jsonDoc["gust"] = windSpeed.getGust();
	mqttPublish(mqttWindSpeedTopic, jsonDoc, true);
	jsonDoc.clear();

	// Publish wind direction to MQTT
	WindDirection windDir1M = windDir.getWindDir(1);
	WindDirection windDir10M = windDir.getWindDir(10);

	jsonDoc["name"] = windDir1M.name;
	jsonDoc["degree"] = windDir1M.degree;
	jsonDoc["name10M"] = windDir10M.name;
	jsonDoc["degree10M"] = windDir10M.degree;
	mqttPublish(mqttWindDirTopic, jsonDoc, true);
	jsonDoc.clear();

	// Publish wind direction to MQTT
	jsonDoc["millis"] = millis();
	mqttPublish(mqttUptimeTopic, jsonDoc, false);
}

void temperature()
{
	// Send the command to get temperatures
	sensors.requestTemperatures();

	// Round value from temperature sensor
	float temp = sensors.getTempCByIndex(0);
	double value = roundValue(temp, 1);

	// Publish temperature to MQTT
	StaticJsonDocument<200> jsonDoc;
	jsonDoc["value"] = value;
	mqttPublish(mqttTempTopic, jsonDoc, true);
}

double roundValue(float value, double noOfDecimals)
{
	double decimals = pow(10, noOfDecimals);
	return (int)(value * decimals + 0.5) / decimals;
}