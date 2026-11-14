#include <Wire.h>
#include <BH1750.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>

// --- Wi-Fi & Raspberry Pi 4 MQTT Broker Settings ---
const char* WIFI_SSID     = "Navpreet";
const char* WIFI_PASSWORD = "NAVPREET";
const char* MQTT_BROKER   = "192.168.137.60";
const int   MQTT_PORT     = 1883;

// --- MQTT Topics ---
const char* TOPIC_TELEMETRY = "assisted_living/telemetry";
const char* TOPIC_COMMAND   = "assisted_living/command";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
BH1750 lightMeter;

// --- Pin Definitions ---
const int PIR_PIN     = 2;  // D2 - PIR Motion Sensor
const int TRIG_PIN    = 3;  // D3 - Ultrasonic TRIG
const int ECHO_PIN    = 4;  // D4 - Ultrasonic ECHO
const int BUTTON_PIN  = 5;  // D5 - Emergency Push Button
const int PATHWAY_LED = 6;  // D6 - Pathway Night-Light LED
const int PLANT_LED   = 7;  // D7 - Plant Moisture Alert LED
const int BUZZER_PIN  = 8;  // D8 - Buzzer
const int ALERT_LED   = 9;  // D9 - Emergency Alert LED
const int SOIL_PIN    = A0; // A0 - Soil Moisture Sensor

// --- Thresholds & Filters ---
const float DARK_ON_LUX        = 40.0;
const float DARK_OFF_LUX       = 65.0;
const float BED_EXIT_CM        = 20.0;
const int   DRY_SOIL_THRESHOLD = 600;

// --- State & Window Trackers ---
bool isDarkState               = false;
bool emergencyActive           = false;
bool manualNightLightOverride  = false;
bool lastButtonState           = HIGH;
int  ultrasonicHitCount        = 0;
float minDistInWindow          = 999.0;
bool pirSeenInWindow           = false;
unsigned long lastActivityTime = 0;
unsigned long lastPublishTime  = 0;
const unsigned long PUBLISH_INTERVAL = 1500;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  while (WiFi.begin(WIFI_SSID, WIFI_PASSWORD) != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.print("\nWi-Fi Connected! Arduino IP: ");
  Serial.println(WiFi.localIP());
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.print("Caregiver Command Received: ");
  Serial.println(msg);

  if (msg == "RESET_EMERGENCY") {
    emergencyActive = false;
    digitalWrite(ALERT_LED, LOW);
    noTone(BUZZER_PIN);
  } else if (msg == "LIGHT_ON") {
    manualNightLightOverride = true;
  } else if (msg == "LIGHT_AUTO") {
    manualNightLightOverride = false;
  }
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to Raspberry Pi MQTT Broker (192.168.137.60)...");
    if (mqttClient.connect("Nano33IoT_AssistedLiving")) {
      Serial.println(" CONNECTED!");
      mqttClient.subscribe(TOPIC_COMMAND);
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2s...");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(PATHWAY_LED, OUTPUT);
  pinMode(PLANT_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(ALERT_LED, OUTPUT);

  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("WARNING: BH1750 not detected on A4/A5!");
  }

  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  connectMQTT();
}

float getCleanDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  if (duration == 0) return 999.0;
  float cm = (duration * 0.0343) / 2.0;
  if (cm < 2.5) return 999.0;
  return cm;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // 1. Light Sensor (Auto Night-Light + Caregiver Override)
  float lux = lightMeter.readLightLevel();
  if (lux >= 0) {
    if (lux < DARK_ON_LUX) isDarkState = true;
    else if (lux > DARK_OFF_LUX) isDarkState = false;
  }
  bool pathwayOn = (isDarkState || manualNightLightOverride);
  digitalWrite(PATHWAY_LED, pathwayOn ? HIGH : LOW);

  // 2. PIR & Continuous Ultrasonic Tracking
  int motion   = digitalRead(PIR_PIN);
  float distCm = getCleanDistance();

  if (motion == HIGH) pirSeenInWindow = true;
  if (distCm < minDistInWindow) minDistInWindow = distCm;

  if (distCm < BED_EXIT_CM) ultrasonicHitCount++;
  else ultrasonicHitCount = 0;

  if (motion == HIGH || ultrasonicHitCount >= 2) {
    lastActivityTime = millis();
  }
  bool activityDetected = (millis() - lastActivityTime < 4000) && (lastActivityTime > 0);

  // 3. Soil Moisture
  int soilRaw = analogRead(SOIL_PIN);
  bool plantNeedsWater = (soilRaw > DRY_SOIL_THRESHOLD);
  digitalWrite(PLANT_LED, plantNeedsWater ? HIGH : LOW);

  // 4. Emergency Push Button Latch
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    emergencyActive = !emergencyActive;
    digitalWrite(ALERT_LED, emergencyActive ? HIGH : LOW);
    if (emergencyActive) tone(BUZZER_PIN, 1000, 300);
    else noTone(BUZZER_PIN);
  }
  lastButtonState = currentButtonState;

  // 5. Publish JSON Telemetry to Raspberry Pi 4 Every 1.5 Seconds
  if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = millis();

    String json = "{";
    json += "\"lux\":" + String(lux, 1) + ",";
    json += "\"dark\":" + String(isDarkState ? "true" : "false") + ",";
    json += "\"pathway_led\":" + String(pathwayOn ? "true" : "false") + ",";
    json += "\"pir\":" + String(pirSeenInWindow ? "true" : "false") + ",";
    json += "\"dist\":" + String(minDistInWindow, 1) + ",";
    json += "\"activity\":" + String(activityDetected ? "true" : "false") + ",";
    json += "\"soil\":" + String(soilRaw) + ",";
    json += "\"plant_dry\":" + String(plantNeedsWater ? "true" : "false") + ",";
    json += "\"emergency\":" + String(emergencyActive ? "true" : "false");
    json += "}";

    mqttClient.publish(TOPIC_TELEMETRY, json.c_str());
    Serial.println("Published: " + json);

    minDistInWindow = 999.0;
    pirSeenInWindow = false;
  }

  delay(50);
}