#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>

// WiFi details
const char* ssid = "HJ";
const char* password = "hj12345678";

// Raspberry Pi MQTT server details
const char* mqttServer = "10.106.187.92";
const int mqttPort = 1883;

const char* mqttTopic = "villageguardian/sensors";

// MQTT login details (leave empty if authentication is disabled)
const char* mqttUser = "";
const char* mqttPassword = "";

// Pin connections
#define DHT_PIN 2
#define DHT_TYPE DHT22

#define PIR_PIN 3
#define BUTTON_PIN 4

#define RED_LED 5
#define GREEN_LED 6
#define BLUE_LED 7

#define RAIN_PIN A0
#define WATER_PIN A1

// Starting the sensors and network clients
DHT dht(DHT_PIN, DHT_TYPE);
BH1750 lightSensor;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Sensor warning levels
const int waterLimit = 300;
const int rainLimit = 700;

// Time settings
unsigned long lastUpdate = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastMqttCheck = 0;

const unsigned long updateInterval = 2000;
const unsigned long reconnectInterval = 5000;

// Variables for storing sensor readings
float temperature = -1;
float humidity = -1;
float lightLevel = -1;

int rainValue = 0;
int waterValue = 0;

bool motion = false;
bool emergencyButton = false;

String systemStatus = "NORMAL";

// Set the RGB LED colour
// This code uses a common-cathode RGB LED.

void setLED(int red, int green, int blue) {
  analogWrite(RED_LED, red);
  analogWrite(GREEN_LED, green);
  analogWrite(BLUE_LED, blue);
}

// Green means the system is normal
void showNormal() {
  setLED(0, 255, 0);
}

// Yellow means a warning has been detected
void showWarning() {
  setLED(255, 255, 0);
}

// Red means an emergency has been detected
void showEmergency() {
  setLED(255, 0, 0);
}

// Connect the Arduino to WiFi
void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWifiCheck < reconnectInterval) {
    return;
  }

  lastWifiCheck = millis();

  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);
}

// Connect to the Raspberry Pi MQTT broker
void connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  if (millis() - lastMqttCheck < reconnectInterval) {
    return;
  }

  lastMqttCheck = millis();

  Serial.println("Connecting to MQTT...");

  String clientName = "VillageGuardian-";
  clientName += String(random(0xFFFF), HEX);

  bool connected;

  // Check whether the MQTT server needs a username
  if (strlen(mqttUser) > 0) {

    connected = mqttClient.connect(
      clientName.c_str(),
      mqttUser,
      mqttPassword
    );

  } else {

    connected = mqttClient.connect(clientName.c_str());

  }

  if (connected) {
    Serial.println("MQTT connection successful!");
  } else {
    Serial.print("MQTT connection failed. Error: ");
    Serial.println(mqttClient.state());
  }
}

// Read the values from all connected sensors
void readSensors() {

  // Read temperature and humidity from DHT22
  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  // Keep the last valid reading if the sensor fails
  if (!isnan(newTemperature)) {
    temperature = newTemperature;
  }

  if (!isnan(newHumidity)) {
    humidity = newHumidity;
  }

  // Read light intensity from BH1750
  float newLight = lightSensor.readLightLevel();

  if (!isnan(newLight) && newLight >= 0) {
    lightLevel = newLight;
  }

  // Read rain sensor value
  rainValue = analogRead(RAIN_PIN);

  // Read water level sensor value
  waterValue = analogRead(WATER_PIN);

  // Check whether motion is detected
  motion = digitalRead(PIR_PIN) == HIGH;

  // The button uses INPUT_PULLUP, so LOW means pressed
  emergencyButton = digitalRead(BUTTON_PIN) == LOW;
}

// Check the readings and decide the system status
void checkSystemStatus() {

  // Emergency button or high water level
  if (emergencyButton || waterValue >= waterLimit) {

    systemStatus = "EMERGENCY";
    showEmergency();

  }

  // Motion or rain warning
  else if (motion || rainValue < rainLimit) {

    systemStatus = "WARNING";
    showWarning();

  }

  // No warning conditions detected
  else {

    systemStatus = "NORMAL";
    showNormal();

  }
}

// Display all sensor readings in the Serial Monitor
void showSensorReadings() {

  Serial.println();
  Serial.println("================================");
  Serial.println("       VILLAGE GUARDIAN");
  Serial.println("================================");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("Light Level: ");
  Serial.print(lightLevel);
  Serial.println(" lux");

  Serial.print("Rain Sensor: ");
  Serial.println(rainValue);

  Serial.print("Water Level: ");
  Serial.println(waterValue);

  Serial.print("Motion: ");
  Serial.println(motion ? "Detected" : "Not Detected");

  Serial.print("Emergency Button: ");
  Serial.println(emergencyButton ? "Pressed" : "Not Pressed");

  Serial.print("System Status: ");
  Serial.println(systemStatus);

  Serial.println("================================");
}

// Send sensor readings to the MQTT server
void sendSensorData() {

  if (!mqttClient.connected()) {
    Serial.println("MQTT is not connected. Data was not sent.");
    return;
  }

  // Prepare sensor readings in JSON format
  char message[350];

  snprintf(
    message,
    sizeof(message),

    "{"
    "\"temperature\":%.2f,"
    "\"humidity\":%.2f,"
    "\"light\":%.2f,"
    "\"rain\":%d,"
    "\"water\":%d,"
    "\"motion\":%s,"
    "\"emergency_button\":%s,"
    "\"status\":\"%s\""
    "}",

    temperature,
    humidity,
    lightLevel,
    rainValue,
    waterValue,
    motion ? "true" : "false",
    emergencyButton ? "true" : "false",
    systemStatus.c_str()
  );

  // Publish the message to the project topic
  if (mqttClient.publish(mqttTopic, message)) {

    Serial.println("Sensor data sent to MQTT:");
    Serial.println(message);

  } else {

    Serial.println("Could not send sensor data.");

  }
}

// Setup runs once when the Arduino starts
void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Starting Village Guardian...");

  // Set sensor pin modes
  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Set RGB LED pins as outputs
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  showNormal();

  // Start the DHT22 sensor
  dht.begin();

  // Start communication with BH1750
  Wire.begin();

  if (lightSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 is ready.");
  } else {
    Serial.println("BH1750 could not be started.");
  }

  // Set up the MQTT server
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setKeepAlive(30);
  mqttClient.setBufferSize(512);

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
}

// Main program keeps running
void loop() {

  // Check the WiFi connection
  connectWiFi();

  // Check the MQTT connection
  connectMQTT();

  // Keep the MQTT connection active
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Update the sensor readings every two seconds
  if (millis() - lastUpdate >= updateInterval) {

    lastUpdate = millis();

    // Read all connected sensors
    readSensors();

    // Check whether the system needs an alert
    checkSystemStatus();

    // Display readings on the Serial Monitor
    showSensorReadings();

    // Send the latest readings to Raspberry Pi
    sendSensorData();
  }
}