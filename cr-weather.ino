#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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
unsigned long lastMsg = 0;
char msg[MQTT_BUFFER_SIZE];

// Wind speed
byte windSpeedCnt = 0;
unsigned long lastWindSpeedTime = 0;

// Rain gauge
long rainCnt = 0;
const float rainPerTipping = 0.2794;
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
	{"SWW", 247.5, 569, 629},
	{"SW", 225.0, 598, 661},
	{"NWN", 337.5, 667, 737},
	{"N", 0.0, 746, 824},
	{"WNW", 292.5, 785, 868},
	{"NW", 315.0, 842, 931},
	{"W", 270.0, 897, 992},
};

void ICACHE_RAM_ATTR windSpeedIsr()
{
	unsigned long now = millis();

	if (now - lastWindSpeedTime > 250)
	{
		windSpeedCnt += 1;
		lastWindSpeedTime = now;
	}
}

void ICACHE_RAM_ATTR rainIsr()
{
	unsigned long now = millis();

	if (now - lastRainTime > 250)
	{
		rainCnt += 1;
		lastRainTime = now;
	}
}

void setup()
{
	Serial.begin(9600);

	setupWifi();
	setupMqtt();

	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(D5, INPUT_PULLUP);
	pinMode(D6, INPUT_PULLUP);

	attachInterrupt(D5, windSpeedIsr, FALLING);
	attachInterrupt(D6, rainIsr, FALLING);
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
}

void everyTenSeconds(bool newSecond)
{
	if (!newSecond)
		return;

	if (second % 10 > 0)
		return;

	Serial.println("Every ten seconds event");

	StaticJsonDocument<200> doc;
	JsonObject jsonTime = doc.createNestedObject("time");
	jsonTime["day"] = day;
	jsonTime["hour"] = hour;
	jsonTime["minute"] = minute;
	jsonTime["second"] = second;

	JsonObject jsonWindDir = doc.createNestedObject("windDirection");
	jsonWindDir["name"] = windDirections[windDirectionIdx].name;
	jsonWindDir["degree"] = windDirections[windDirectionIdx].degree;

	serializeJson(doc, msg);

	// Publish to MQTT
	// snprintf(msg, MQTT_BUFFER_SIZE, "hello world #%ld", second);
	Serial.print("Publish message: ");
	Serial.println(msg);
	client.publish("weather/everyTenSeconds", msg);
}

void everyMinute(bool newSecond)
{
	if (!newSecond)
		return;

	if (second > 0)
		return;

	Serial.println("Every minute event");
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
}

void rain()
{
}

void windSpeed()
{
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

int calcAverage(int lastAvg, int newValue, int samples)
{
	return (((lastAvg * (samples - 1)) + newValue) / samples);
}
