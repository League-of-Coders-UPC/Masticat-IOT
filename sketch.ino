#include <Arduino.h>
#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi Configuration
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* BASE_URL = "https://edge-masticat.sfo1.zeabur.app/api/v1";

// Hardware Pins
#define TOTAL_FOOD_DOUT 32
#define TOTAL_FOOD_SCK 33
#define TRAY_FOOD_DOUT 15
#define TRAY_FOOD_SCK 23
#define TOTAL_WATER_TRIG 25
#define TOTAL_WATER_ECHO 26
#define TRAY_WATER_TRIG 27
#define TRAY_WATER_ECHO 14
#define SERVO_FOOD_PIN 13
#define SERVO_WATER_PIN 12
#define START_FILL_BUTTON 16
#define END_FILL_BUTTON 17
#define DISPENSE_100G_BUTTON 18
#define DISPENSE_50ML_BUTTON 19

// Container Configuration
#define TOTAL_WATER_CONTAINER_HEIGHT 200.0
#define WATER_CONTAINER_OFFSET 100
#define FOOD_CALIBRATION_FACTOR 0.42
#define WATER_CALIBRATION_FACTOR 0.42

// Objects
HX711 totalFoodScale;
HX711 trayFoodScale;
Servo foodDispenser;
Servo waterDispenser;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// State Variables
String serialNumber = "";
String deviceId = "";
float foodQuantity = 0.0;
float waterQuantity = 0.0;
float foodLimit = 0;
float waterLimit = 0;
bool isFillingProcess = false;
float initialTotalFood = 0.0;
float initialTotalWater = 0.0;
bool isDispensing = false;


struct FillValidationResult {
  float addedFood;
  float addedWater;
  bool valid;
};

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  connectToWiFi();
  
  // Initialize Scales
  totalFoodScale.begin(TOTAL_FOOD_DOUT, TOTAL_FOOD_SCK);
  totalFoodScale.set_scale(FOOD_CALIBRATION_FACTOR);
  totalFoodScale.tare();
  
  trayFoodScale.begin(TRAY_FOOD_DOUT, TRAY_FOOD_SCK);
  trayFoodScale.set_scale(FOOD_CALIBRATION_FACTOR);
  trayFoodScale.tare();
  
  // Initialize Servos
  foodDispenser.attach(SERVO_FOOD_PIN);
  waterDispenser.attach(SERVO_WATER_PIN);
  
  // Initialize LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Iniciando...");
  
  // Initialize Buttons
  pinMode(START_FILL_BUTTON, INPUT_PULLUP);
  pinMode(END_FILL_BUTTON, INPUT_PULLUP);
  pinMode(DISPENSE_100G_BUTTON, INPUT_PULLUP);
  pinMode(DISPENSE_50ML_BUTTON, INPUT_PULLUP);
  
  // Get Serial Number
  serialNumber = getSerialNumber();
  displaySerialNumber();
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
}

String getSerialNumber() {
  return WiFi.macAddress();
}

void displaySerialNumber() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Serial Number:");
  lcd.setCursor(0, 1);
  lcd.print(serialNumber);
  Serial.println(serialNumber);
}

bool fetchDeviceInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(BASE_URL) + "/device?macAddress=" + serialNumber;
    http.begin(url);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String payload = http.getString();
      
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        deviceId = doc["id"].as<String>();
        foodQuantity = doc["food_quantity"];
        waterQuantity = doc["water_quantity"];
        foodLimit = doc["food_limit"];
        waterLimit = doc["water_limit"];
        
        return true;
      }
    }
    http.end();
  }
  return false;
}

void startFillProcess() {
  isFillingProcess = true;
  initialTotalFood = totalFoodScale.get_units();
  initialTotalWater = measureWaterLevel(TOTAL_WATER_TRIG, TOTAL_WATER_ECHO, TOTAL_WATER_CONTAINER_HEIGHT);
  
  lcd.clear();
  lcd.print("Fill Process...");
}

float measureWaterLevel(int trigPin, int echoPin, float height) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  float distance = (duration / 58.0) - WATER_CONTAINER_OFFSET;
  float waterHeight = height - distance;
  float waterVolume = (waterHeight / height) * waterLimit;
  
  return waterVolume > 0 ? waterVolume : 0;
}

FillValidationResult validateFillQuantity() {
  FillValidationResult result;
  
  float currentTotalFood = totalFoodScale.get_units();
  float currentTotalWater = measureWaterLevel(TOTAL_WATER_TRIG, TOTAL_WATER_ECHO, TOTAL_WATER_CONTAINER_HEIGHT);
  
  result.addedFood = currentTotalFood - initialTotalFood;
  result.addedWater = currentTotalWater - initialTotalWater;
  
  if (result.addedFood + foodQuantity > foodLimit) {
    lcd.clear();
    lcd.print("Food Overflow!");
    result.valid = false;
    return result;
  }
  
  if (result.addedWater + waterQuantity > waterLimit) {
    lcd.clear();
    lcd.print("Water Overflow!");
    result.valid = false;
    return result;
  }
  
  result.valid = true;
  return result;
}

void makePostRequest(String type, float quantity, String action) {
  String jsonBody = "{\"deviceId\":\"" + deviceId + "\","
                    "\"type\":\"" + type + "\","
                    "\"quantity\":" + String(quantity) + ","
                    "\"action\":\"" + action + "\"}";

  HTTPClient http;
  String url = String(BASE_URL) + "/dispense-request";
  http.begin(url);

  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonBody);
  
  if (httpResponseCode > 0) {
    String payload = http.getString();
      
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        foodQuantity = doc["food_quantity"];
        waterQuantity = doc["water_quantity"];
      }
  }

  http.end();
}


void endFillProcess() {
  FillValidationResult validationResult = validateFillQuantity();
  if (!validationResult.valid) return;
  
  if (validationResult.addedFood > 0) {
    makePostRequest("food", validationResult.addedFood, "add");
    lcd.clear();
    lcd.print("Food: " + String(foodQuantity) + "g");
  }
  if (validationResult.addedWater > 0) {
    makePostRequest("water", validationResult.addedWater, "add");
    lcd.clear();
    lcd.print("Water: " + String(waterQuantity) + "ml");
  }
}

void startFoodDispensing(float amount) {
  isDispensing = true;

  float initialWeight = trayFoodScale.get_units();
  
  foodDispenser.write(180);
  
}

void checkAndDispenseFood(float previousFoodQuantity, float actualQuanitty) {
  float difference = previousFoodQuantity - actualQuanitty;
  
  if (difference > 0 && !isDispensing) {
    
    startFoodDispensing(difference);
  }
  
  if (isDispensing) {
    monitorFoodDispensing(difference);
  }
}

void monitorFoodDispensing(float targetAmount) {
  float currentWeight = trayFoodScale.get_units();
  float initialWeight = trayFoodScale.get_units() - targetAmount;
  float dispensedAmount = currentWeight - initialWeight;
  
  if (dispensedAmount >= targetAmount) {
    stopFoodDispensing();
    return;
  }
}

void stopFoodDispensing() {
  foodDispenser.write(0);
  isDispensing = false;
}

void loop() {
  static unsigned long lastDeviceCheckTime = 0;
  
  if (millis() - lastDeviceCheckTime > 5000) {
    Serial.println(deviceId);
    fetchDeviceInfo();
    lastDeviceCheckTime = millis();
  }
  
  // Button handlers
  if (digitalRead(START_FILL_BUTTON) == LOW) {
    startFillProcess();
  }
  
  if (digitalRead(END_FILL_BUTTON) == LOW) {
    endFillProcess();
  }
  
}