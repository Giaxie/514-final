#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include "MAX30105.h"

const int GSR_PIN = 34;   // GSR sensor connected to GPIO34
MAX30105 particleSensor;

// Data structure to send sensor readings
struct struct_message {
  int gsr;
  float stressIndex;
  int32_t red;
  int32_t ir;
};
struct_message sensorData;

// Target display module's MAC address (adjust as needed)
uint8_t displayAddress[] = {0x3C, 0x8A, 0x1F, 0xA1, 0x8A, 0xCC};

// ESP-NOW send callback
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

/* 
  Compute Stress Index:
  
  Explanation for a high school student:
  - The GSR sensor measures skin resistance. When you are stressed, you sweat more, and your skin resistance decreases.
  - We define a baseline (here, 1000) such that if the GSR value equals 1000, we treat it as 100 (maximum stress), and if it equals 4095 (fully relaxed or no contact), we treat it as 0.
  - The MAX30102 sensor provides IR and RED signals related to blood flow. Their changes are smaller, so we give them a 20% weight.
  - Final Stress Index = 0.8 * (Normalized GSR) + 0.2 * (IR+RED contribution), and we keep the value between 0 and 100.
*/
float computeStressIndex(int gsr, int32_t ir, int32_t red) {
  float baseline = 1000.0; // assumed baseline value when highly stressed
  // Normalize GSR: when gsr == baseline -> 100, when gsr == 4095 -> 0
  float normGSR = (4095 - gsr) * 100.0 / (4095 - baseline);
  normGSR = constrain(normGSR, 0, 100);
  
  // IR+RED contribution (using the same scaling as before)
  float irRedContribution = ((float)(ir + red)) / 200000.0 * 100.0;
  
  // Compute raw stress index as before
  float rawStress = 0.8 * normGSR + 0.2 * irRedContribution;
  // Constrain rawStress to expected range (if sensor readings behave this way)
  rawStress = constrain(rawStress, 92, 100);
  
  // Remap rawStress to a new range:
  // For rawStress in [92, 95] map linearly to [20, 60]
  // For rawStress in [95, 100] map linearly to [60, 80]
  float newStress;
  if (rawStress <= 95) {
    newStress = (rawStress - 92) * (60 - 20) / (95 - 92) + 20;
  } else {
    newStress = (rawStress - 95) * (80 - 60) / (100 - 95) + 60;
  }
  
  newStress = constrain(newStress, 0, 100);
  return newStress;
}

void setup() {
  Serial.begin(115200);
  pinMode(GSR_PIN, INPUT);
  Wire.begin(21, 22);

  // Initialize MAX30102 sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);  // Red LED low amplitude
  particleSensor.setPulseAmplitudeGreen(0);   // Turn off green LED

  // Initialize WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, displayAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Adding peer failed");
    return;
  }

  Serial.println("Sensor Module Initialized");
}

void loop() {
  int gsrValue = analogRead(GSR_PIN);
  long irValue = particleSensor.getIR();
  int32_t redValue = particleSensor.getRed();

  float stressIndex = computeStressIndex(gsrValue, irValue, redValue);

  sensorData.gsr = gsrValue;
  sensorData.stressIndex = stressIndex;
  sensorData.red = redValue;
  sensorData.ir = irValue;

  Serial.printf("GSR: %d, Stress Index: %.2f, IR: %ld, RED: %ld\n", gsrValue, stressIndex, irValue, redValue);

  esp_err_t result = esp_now_send(displayAddress, (uint8_t *)&sensorData, sizeof(sensorData));
  if (result == ESP_OK) {
    Serial.println("Data sent successfully");
  } else {
    Serial.print("Send error: ");
    Serial.println(result);
  }
  
  delay(1000);  // Update every 1 second
}
