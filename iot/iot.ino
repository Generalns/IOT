#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "HX711.h"

#define LOADCELL_DOUT_PIN 4
#define LOADCELL_SCK_PIN 5
#define TEMP_PIN A0

const float B = 3950;
const float R0 = 100000;
float santigrat;

HX711 scale;
float calibration_factor = 211;
float units;

// WiFi credentials
const char *ssid = "FiberHGW_ZTUE5Y_2.4GHz";
const char *password = "qfUNdRtfd9";

// API details
const char *apiUrl = "http://192.168.1.45";
const int apiPort = 8000;
const char *apiEndpoint = "/api/sensor";

void setup() {
  Serial.begin(9600);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println("Sa baris1");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  Serial.println("Sa baris2");

  scale.set_scale(calibration_factor);
  Serial.println("Sa barisyeni");

  Serial.println("Sa baris3");

}

void loop() {
  scale.power_up();

  Serial.println("Sa baris4");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected to WiFi. Retrying...");
    delay(1000);
    return;
  }

  int sensorValue = analogRead(TEMP_PIN);

  float R = R0 * (1023.0 / sensorValue - 1.0);
  santigrat = 1.0 / ((log(R / R0) / B) + (1.0 / 298.15)) - 273.15;

  //units = scale.get_units();
  Serial.println("Sa baris5");

  scale.power_down();

  Serial.print("Reading: ");
  Serial.print(units);
  Serial.print(" grams,   ");
  Serial.print(santigrat);
  Serial.println(" °C ");

  // Send sensor data to API using HTTP POST request
  sendSensorData(units, santigrat);

  delay(5000); // Adjust the delay based on your requirements
}


void sendSensorData(float weight, float temperature) {
  WiFiClient client;  // Use WiFiClient instead of HTTPClient

  // Construct the API URL
  String apiUrlWithEndpoint = String(apiUrl) + ":" + String(apiPort) + String(apiEndpoint) + "?weight=" + String(weight) + "&temperature=" + String(temperature);

  // Build the POST request payload
  String payload = "?weight=" + String(weight) + "&temperature=" + String(temperature);

  HTTPClient http;
  http.begin(client, apiUrlWithEndpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  Serial.println(http.getString());
  // Send the POST request
  int httpCode = http.POST("");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("POST request sent successfully");
    Serial.println("Server response: " + response);
  } else {
    Serial.println("Error sending POST request");
  }

  http.end();
}