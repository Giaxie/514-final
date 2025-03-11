#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Stepper Motor configuration (4-wire)
#define STEPPER_PIN1 16
#define STEPPER_PIN2 17
#define STEPPER_PIN3 18
#define STEPPER_PIN4 19
AccelStepper stepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN2, STEPPER_PIN3, STEPPER_PIN4);
const long gaugeRangeSteps = 2048;  // Total steps for full gauge movement
const float gaugeMin = 0.0;
const float gaugeMax = 100.0;

// LED pins for status indication
#define LED_RED    14
#define LED_YELLOW 26
#define LED_GREEN  27

// Data structure (must match sensor module)
struct struct_message {
  int gsr;
  float stressIndex;
  int32_t red;
  int32_t ir;
};
struct_message receivedData;
volatile bool dataReceived = false;

// ESP-NOW receive callback
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(receivedData)) {
    memcpy(&receivedData, incomingData, sizeof(receivedData));
    dataReceived = true;
  }
}

// Update LED state based on Stress Index:
// Stress < 33: Green; 33 <= Stress < 66: Yellow; Stress >= 66: Red.
void updateLED(float stressIndex) {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);
  
  if (stressIndex < 21.0) {
    digitalWrite(LED_GREEN, HIGH);
  } else if (stressIndex < 50.0) {
    digitalWrite(LED_YELLOW, HIGH);
  } else {
    digitalWrite(LED_RED, HIGH);
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);

  // Initialize I2C for OLED (ensure pins match your wiring)
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 Initialization Failed!");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  // Initialize stepper motor parameters
  stepper.setMaxSpeed(1000);
  stepper.runSpeedToPosition(); 

  // Initialize WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Display Module Initialized");
}

void loop() {
  if (dataReceived) {
    dataReceived = false;
    
    // Update OLED display with sensor data
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.printf("GSR: %d\n", receivedData.gsr);
    display.printf("Stress: %.2f\n", receivedData.stressIndex);
    display.printf("IR: %ld\n", receivedData.ir);
    display.printf("RED: %ld\n", receivedData.red);
    display.display();
    
    // Update LED status based on Stress Index
    updateLED(receivedData.stressIndex);
    
    // Map Stress Index (0~100) to stepper motor steps (0~1024)
    long targetPosition = (long)((receivedData.stressIndex - gaugeMin) / (gaugeMax - gaugeMin) * gaugeRangeSteps);
    stepper.moveTo(targetPosition);
    
    Serial.printf("Received -> GSR: %d, Stress: %.2f, IR: %ld, RED: %ld\n",
                  receivedData.gsr, receivedData.stressIndex, receivedData.ir, receivedData.red);
  }
  
  // Run the stepper motor gradually to target position
  stepper.run();
  
  delay(1000);  // Update every 1 second
}
