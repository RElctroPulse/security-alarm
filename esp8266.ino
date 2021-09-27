#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoWebsockets.h>

#define WIFI_SSID "Your WiFi SSID"
#define WIFI_PASSWORD "your wifi password"

#define HOST "http://your-heroku-app-url-here"
#define WS_HOST "ws://your-heroku-app-url-here/esp8266"

#define SENSOR_PIN A0
#define BUZZER_PIN D6
#define RELAY_PIN  D4

using namespace websockets;

WebsocketsClient client;
bool autoAlertState = true;
bool useLightsState = true;
bool dismissed = false;

void setup() {
  Serial.begin(19200);
  Serial.setDebugOutput(true);

  pinMode(SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN,  OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, HIGH);

  connectToWiFi();
  connectWS();
}

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP = ");
  Serial.println(WiFi.localIP());
}

void connectWS() {
  // Setup Callbacks
  client.onMessage(onMessageCallback);
  client.onEvent(onEventsCallback);
  // Connect to server
  client.connect(WS_HOST);
}

void onMessageCallback(WebsocketsMessage message) {
  String data = message.data();

  Serial.print("WS: ");
  Serial.println(data);
  
  if (data == "autoalert") {
    autoAlertState = true;
  } else if (data == "noautoalert") {
    autoAlertState = false;
    
  } else if (data == "uselights") {
    useLightsState = true;
  } else if (data == "nouselights") {
    useLightsState = false;

  } else if (data == "alert") {
    playSound();
  } else if (data == "dismiss") {
    dismissed = true;
  }
}

void onEventsCallback(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("Connected to WS");
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("WS connnection closed, trying to reconnect...");
    connectWS();
  }
}

int checkSensor() {
  int val = analogRead(SENSOR_PIN);
  return val < 600;
}

void playSound() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);
}

void turnOnLights() {
  digitalWrite(RELAY_PIN, LOW);
}

void sendAlert() {
  WiFiClient wifi;
  HTTPClient http;
  http.begin(wifi, String(HOST) + "/notify");
  http.POST("");
  http.end();
  Serial.println("Sent alert");
}

void waitForDismis() {
  for (;;) {
    delay(1000);
    client.poll();
    if (dismissed) {
      break;
    }

    if (autoAlertState) {
      playSound();
    }
  }

  return;
}

void loop() {

  bool sensorActive = checkSensor();

  if (dismissed) {
    // Turn off buzzer an lights
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RELAY_PIN, HIGH);

    // If nothing is being detected, set dismissed state to false
    // so we are back to listening and responding to sensor.
    if (!sensorActive) {
       Serial.println("Sensor inactive, starting to listen in 3 seconds...");
       dismissed = false;
    }

    client.poll();
    delay(3000);
    return;
  }

  if (sensorActive) {

    Serial.println("Detected");
    Serial.print("autoAlertState = ");
    Serial.println(autoAlertState);

    if (useLightsState) {
      turnOnLights();
    }

    sendAlert();
    waitForDismis();
  }

  client.poll();
  delay(2000);
}
