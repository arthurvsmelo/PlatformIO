#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <Arduino_JSON.h>

const IPAddress apIP = IPAddress(192,168,4,1);
const IPAddress mask = IPAddress(255,255,255,0);

const char* ssid = "ESP32SSID";
const char* password = "12345678#!";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Json Variable to Hold Sensor Readings
JSONVar readings;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 10;

// Initialize LittleFS
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}


// Get Sensor Readings and return JSON object
String getSensorReadings(){
  readings["epoch"] = lastTime + timerDelay;
  readings["temperature"] = random(16,40);
  readings["humidity"] = random(60,95);
  readings["pressure"] = random(100,760);
  String jsonString = JSON.stringify(readings);
  return jsonString;
}

// Initialize WiFi
void initWiFi() {

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP,apIP,mask);

  Serial.print("Starting WiFi Access Point..");
  while (!WiFi.softAP(ssid,password)) {
    Serial.print('.');
    delay(1000);
  }
}

void notifyClients(String sensorReadings) {
  ws.textAll(sensorReadings);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    //data[len] = 0;
    //String message = (char*)data;
    // Check if the message is "getReadings"
    //if (strcmp((char*)data, "getReadings") == 0) {
      //if it is, send current sensor readings
      String sensorReadings = getSensorReadings();
      Serial.print(sensorReadings);
      notifyClients(sensorReadings);
    //}
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void readAndNotifyClientsFunc(void *parameter){
  for(;;){
      if ((millis() - lastTime) > timerDelay) {
      String sensorReadings = getSensorReadings();
      //Serial.print(sensorReadings);
      notifyClients(sensorReadings);
      lastTime = millis();
    }
    delay(100);
    ws.cleanupClients();
  }
}

TaskHandle_t readAndNotifyTask;

void setup() {
  //Serial.begin(115200);
  initWiFi();
  initLittleFS();
  initWebSocket();

  xTaskCreatePinnedToCore(
          readAndNotifyClientsFunc,   /* Task function. */
          "readAndNotifyClients",     /* name of task. */
          10000,                      /* Stack size of task */
          NULL,                       /* parameter of the task */
          1,                          /* priority of the task */
          &readAndNotifyTask,         /* Task handle to keep track of created task */
          1                           /* Running at second core */
  );                         

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/",LittleFS,"/");

  // Start server
  server.begin();
}

void loop() {
}
