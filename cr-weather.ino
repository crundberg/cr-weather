#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include "time.h"
#include "config.h"
#include "windSpeed.h"
#include "windDir.h"
#include "rain.h"
#include "light.h"

// Wifi
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// MQTT
IPAddress mqttServer MQTT_SERVER_IP;
const char *mqttUser = MQTT_USER;
const char *mqttPassword = MQTT_PASSWORD;
unsigned long mqttLastReconnectAttempt = 0;

WiFiClient espClient;
PubSubClient client(espClient);

// One Wire
OneWire oneWire(9); // TODO: Fix pin number...
DallasTemperature sensors(&oneWire);

// Time
int second = 0;
int minute = 0;
int hour = 0;
time_t rawTime;
struct tm *timeInfo;
int lastSecond = 0;

// Weather meter
WindSpeed windSpeed(WIND_SPEED_PIN);
WindDir windDir(WIND_DIRECTION_PIN, WIND_DIRECTION_OFFSET);
Rain rain(RAIN_PIN);
Light light(LIGHT_ADDR);

void IRAM_ATTR windSpeedIsr()
{
	windSpeed.count();
}

void IRAM_ATTR rainIsr()
{
	rain.count();
}

void setup()
{
	Serial.begin(115200);
	Serial.println();
	Serial.println();

	setupWifi();
	setupMqtt();
	setupOTA();
	setupTime();

	// Setup one wire
	sensors.begin();

	// Setup I2C
	Wire.begin();

	// Setup sensors
	windSpeed.begin();
	windDir.begin();
	rain.begin();
	light.begin(&Wire);

	// Setup LED
	pinMode(LED_BLUE, OUTPUT);

	// Setup interrupts
	attachInterrupt(WIND_SPEED_PIN, windSpeedIsr, FALLING);
	attachInterrupt(RAIN_PIN, rainIsr, FALLING);
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
		Serial.println("[OTA] Start updating " + type); });

	ArduinoOTA.onEnd([]()
					 { Serial.println("\n[OTA] End"); });
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
						  { Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100))); });
	ArduinoOTA.onError([](ota_error_t error)
					   {
   Serial.printf("Error[%u]: ", error);
   if (error == OTA_AUTH_ERROR) {
	Serial.println("[OTA] Auth Failed");
   } else if (error == OTA_BEGIN_ERROR) {
	Serial.println("[OTA] Begin Failed");
   } else if (error == OTA_CONNECT_ERROR) {
	Serial.println("[OTA] Connect Failed");
   } else if (error == OTA_RECEIVE_ERROR) {
	Serial.println("[OTA] Receive Failed");
   } else if (error == OTA_END_ERROR) {
	Serial.println("[OTA] End Failed");
   } });

	ArduinoOTA.begin();
}

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

	windSpeed.loop(minute, second);
	windDir.loop(minute, second);
	rain.loop(hour, minute);
	light.loop(minute, second);

	everySecond(newSecond);
	everyTenSecond(newSecond);
	everyMinute(newSecond);

	mqttLoop();
}

boolean mqttConnect()
{
	Serial.println("[MQTT] Connect");

	if (client.connect("CRWeatherArduino", mqttUser, mqttPassword))
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
			mqttConnect();
		}

		return;
	}

	client.loop();
}

void everySecond(bool newSecond)
{
	if (!newSecond)
		return;

	// Toggle led
	digitalWrite(LED_BLUE, second % 2 == 0);
}

void everyTenSecond(bool newSecond)
{
	if (!newSecond)
		return;

	if (second % 10 > 0)
		return;

	// Serial.println("[Event] Every ten seconds event");
}

void mqttPublish(const char *topic, double value, bool retained)
{
	// Publish to MQTT
	bool result = client.publish(topic, String(value, 2).c_str(), false);

	// Print message
	return;
	Serial.print("[MQTT] Publish ");
	Serial.print(topic);
	Serial.print(": ");
	Serial.print(value);
	Serial.print(" (");
	Serial.print(result);
	Serial.println(")");
}

void mqttPublish(const char *topic, unsigned long value, bool retained)
{
	char buffer[10];
	sprintf(buffer, "%d", value);

	// Publish to MQTT
	bool result = client.publish(topic, buffer, false);

	// Print message
	return;
	Serial.print("[MQTT] Publish ");
	Serial.print(topic);
	Serial.print(": ");
	Serial.print(value);
	Serial.print(" (");
	Serial.print(result);
	Serial.println(")");
}

void mqttPublish(const char *topic, const char *value, bool retained)
{
	// Publish to MQTT
	bool result = client.publish(topic, value, false);

	// Print message
	return;
	Serial.print("[MQTT] Publish ");
	Serial.print(topic);
	Serial.print(": ");
	Serial.print(value);
	Serial.print(" (");
	Serial.print(result);
	Serial.println(")");
}

void publishTemperature()
{
	// Send the command to get temperatures
	sensors.requestTemperatures();

	// Get value from temperature sensor
	float temp = sensors.getTempCByIndex(0);

	// Validate value
	if (temp < -55.0 || temp > 125.0)
	{
		Serial.print("[Temp] Failed to read temperature (value=");
		Serial.print(temp);
		Serial.println(")");
		return;
	}

	// Round value
	double value = roundValue(temp, 1);

	// Publish temperature to MQTT
	char *topic = "weather/temp/value";
	mqttPublish(topic, value, true);
}

void publishRain()
{
	char *topic = "weather/rain/last1M";
	double value = rain.getRainForLastMinutes(1);
	mqttPublish(topic, value, true);

	topic = "weather/rain/last10M";
	value = rain.getRainForLastMinutes(10);
	mqttPublish(topic, value, true);

	topic = "weather/rain/last1H";
	value = rain.getRainForLastMinutes(60);
	mqttPublish(topic, value, true);

	topic = "weather/rain/today";
	value = rain.getRainForToday();
	mqttPublish(topic, value, true);

	topic = "weather/rain/yesterday";
	value = rain.getRainForYesterday();
	mqttPublish(topic, value, true);

	topic = "weather/rain/totaltTicks";
	unsigned long totalTicks = rain.getTotaltTicks();
	mqttPublish(topic, totalTicks, true);
}

void publishWindSpeed()
{
	char *topic = "weather/windSpeed/last1M";
	double value = windSpeed.getWindSpeed(1);
	mqttPublish(topic, value, true);

	topic = "weather/windSpeed/last10M";
	value = windSpeed.getWindSpeed(10);
	mqttPublish(topic, value, true);

	topic = "weather/windSpeed/last1H";
	value = windSpeed.getWindSpeed(60);
	mqttPublish(topic, value, true);

	topic = "weather/windSpeed/gust";
	value = windSpeed.getGust();
	mqttPublish(topic, value, true);
}

void publishWindDir()
{
	WindDirection windDir1M = windDir.getWindDir(1);
	WindDirection windDir10M = windDir.getWindDir(10);

	char *topic = "weather/windDir/avg1M";
	mqttPublish(topic, windDir1M.degree, true);

	topic = "weather/windDir/avg1M/name";
	mqttPublish(topic, windDir1M.name, true);

	topic = "weather/windDir/avg10M";
	mqttPublish(topic, windDir1M.degree, true);

	topic = "weather/windDir/avg10M/name";
	mqttPublish(topic, windDir10M.name, true);
}

void publishLight()
{
	char *topic = "weather/light/value";
	unsigned long value = (double)light.getLux();
	mqttPublish(topic, value, true);

	topic = "weather/light/avg";
	value = 0;
	mqttPublish(topic, value, true);
}

void publishUptime()
{
	char *topic = "weather/debug/uptime";
	unsigned long value = millis();
	mqttPublish(topic, value, true);
}

void everyMinute(bool newSecond)
{
	if (!newSecond)
		return;

	if (second > 0)
		return;

	// Serial.println("[Event] Every minute event");

	publishTemperature();
	publishRain();
	publishWindSpeed();
	publishWindDir();
	publishLight();
	publishUptime();
}

double roundValue(float value, double noOfDecimals)
{
	double decimals = pow(10, noOfDecimals);
	return (int)(value * decimals + 0.5) / decimals;
}
