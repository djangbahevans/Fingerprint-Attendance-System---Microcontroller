#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

DynamicJsonDocument doc(1024);

#define rxPin D5
#define txPin D6
SoftwareSerial s(D5, D6); // rx tx

const char *ssid = "";
const char *password = "";
const char *host = "http://calm-temple-96942.herokuapp.com";

bool timeout = false;

void setup()
{
  Serial.begin(115200);
  s.begin(115200);
  Serial.println("Starting...");

  // Wait until WiFi details received
  delay(2000);
  s.print(waitForWiFiDetails());
  s.print(200);
}

void loop()
{
  String ins = readFromArduino();
  if (ins != "")
  {
    sendData(ins);
  }
}

String readFromArduino()
{
  String json;
  while (s.available())
  {
    json = s.readString();
  }

  return json;
}

void sendData(String data)
{
  Serial.print("connecting to ");
  Serial.println(host); // Use WiFiClient class to create TCP connections
  Serial.println(data);
  WiFiClient client;
  const int httpPort = 80;

  if (!client.connect(host, httpPort))
  {
    Serial.println("connection failed");
    s.print(0);
    return;
  }

  Serial.print("Requesting URL: ");
  String address = host;
  HTTPClient http;
  http.begin(address);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(data);
  s.print(httpCode);
  Serial.println(httpCode); // Print HTTP return code
  String payload = http.getString();
  Serial.println(payload); //Print request response payload
  Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  http.end(); //Close connection Serial.println();
  Serial.println("closing connection");
}

void setupWiFi()
{
  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default, would try to act as both a client and an access-point and could cause network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.println("WiFi connected");
}

int waitForWiFiDetails()
{
  Serial.println("Waiting for WiFi...");
  unsigned long start = millis();
  unsigned long end = millis();
  while (!s.available())
  {
    end = millis();
    if ((end - start) >= 10000) // Timeout
    {
      timeout = true;
      Serial.println("Timeout");
      break;
    }
  }
  if (!timeout)
  {
    String json = s.readString();
    Serial.print("JSON: ");
    Serial.println(json);
    deserializeJson(doc, json);
    ssid = doc["ssid"];
    password = doc["password"];
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Pass: ");
    Serial.println(password);
    setupWiFi();

    return 200;
  } else {
    Serial.println("Timeout");
    return 100;
  }
}