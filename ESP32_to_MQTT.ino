
#include <WiFi.h>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <AsyncMqttClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define WIFI_SSID "JAVIERtest"
#define WIFI_PASSWORD "JavierTest"

#define MQTT_HOST IPAddress( 192, 168, 137, 4)
#define MQTT_PORT 1883


#define MQTT_PUB_TEMP "Sensing_board1/Temperature "
#define MQTT_PUB_HUM "Sensing_board1/Humidity"
#define MQTT_SUB_LIGHT "Sensing_board1/Light"

const int oneWireBus = 4;
#define SensorPin 34  // used for ESP32
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);

float temp = sensors.getTempCByIndex(0);
float hum = analogRead(SensorPin);

// use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0     0

// use 13 bit precission for LEDC timer
#define LEDC_TIMER_13_BIT  13

// use 5000 Hz as a LEDC base frequency
#define LEDC_BASE_FREQ     5000

// fade LED PIN (replace with LED_BUILTIN constant for built-in LED)
#define LED_PIN            32

int brightness = 0;    // how bright the LED is
int fadeAmount = 5;    // how many points to fade the LED by

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;


void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  // calculate duty, 8191 from 2 ^ 13 - 1
  uint32_t duty = (8191 / valueMax) * min(value, valueMax);

  // write duty to LEDC
  ledcWrite(channel, duty);
}


void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  uint16_t packetIdSub = mqttClient.subscribe(MQTT_SUB_LIGHT, 0);
  Serial.print("Subscribing at QoS 0, packetId: ");               //  "Подписка при QoS 0, ID пакета: "
  Serial.println(packetIdSub);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}


void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  String messageTemp;
  for (int i = 0; i < len; i++) {
    //Serial.print((char)payload[i]);
    messageTemp += (char)payload[i];
  }
  // проверяем, получено ли MQTT-сообщение в топике «esp32/led»:
  if (strcmp(topic, "esp32/led") == 0) {
    while (strcmp(topic, "esp32/led") != brightness) { // если светодиод выключен, включаем его (и наоборот):
      // set the brightness on LEDC channel 0
      ledcAnalogWrite(LEDC_CHANNEL_0, brightness);

      // change the brightness for next time through the loop:
      if ( strcmp(topic, "esp32/led") > brightness) {
        brightness = brightness + fadeAmount;
      }
      // reverse the direction of the fading at the ends of the fade:
      else  {
        fadeAmount = -fadeAmount;
        brightness = brightness + fadeAmount;
      }
      Serial.println(brightness);
      // wait for 30 milliseconds to see the dimming effect
      delay(300);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();


  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  sensors.begin();
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(LED_PIN, LEDC_CHANNEL_0);

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();
  delay(2000);
}

void loop() {
  hum = analogRead(SensorPin);
  temp = sensors.getTempCByIndex(0);
  hum = map(hum, 2250, 4095,100, 0);
  sensors.requestTemperatures();
  // Publish an MQTT message on topic esp32/BME2800/temperature
  uint16_t packetIdPub1 = mqttClient.publish(MQTT_PUB_TEMP, 1, true, String(temp).c_str());
  Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", MQTT_PUB_TEMP, packetIdPub1);
  Serial.printf("Message: %.2f \n", temp);
  delay(2000);
  // Publish an MQTT message on topic esp32/BME2800/humidity
  uint16_t packetIdPub2 = mqttClient.publish(MQTT_PUB_HUM, 1, true, String(hum).c_str());
  Serial.printf("Publishing on topic %s at QoS 1, packetId %i: ", MQTT_PUB_HUM, packetIdPub2);
  Serial.printf("Message: %.2f \n", hum);
  delay(2000);
  // Publish an MQTT message on topic esp32/BME2800/humidity
  uint16_t packetIdSub = mqttClient.subscribe(MQTT_SUB_LIGHT, 1);
  Serial.printf("hearing on topic %s at QoS 1, packetId %i: ", MQTT_SUB_LIGHT, packetIdSub);
  Serial.printf("Message: %.2f \n", brightness);
  delay(2000);
  delay(2000);
}
