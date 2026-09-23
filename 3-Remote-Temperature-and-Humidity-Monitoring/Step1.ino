#include <WebServer.h>
#include <WiFi.h>
#include "DHT.h"          // Temp & Humidity sensor driver
#include "SSD1306Wire.h"  // OLED display driver

#define DHTPIN 2          // DHT sensor pin (also TB6612 AIN1 on this board)
#define DHTTYPE DHT11     // DHT11 sensor

#define BUZZER_PIN 9      // buzzer pin
#define BUZZER_CHANNEL 0  // LEDC channel for buzzer

// TB6612 motor pins on this board — hold them off so DHT use of GPIO 2
// (AIN1) does not spin the left motor when PWMA floats.
#define DRV_PWMA  43
#define DRV_A2    1
#define DRV_PWMB  44
#define DRV_B1    4
#define DRV_B2    3

#define SDA_PIN D4        // I2C SDA
#define SCL_PIN D5        // I2C SCL
#define OLED_ADDR 0X3C    // I2C address
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define TEMP_THRESHOLD 24.0   // Celsius
#define HUMI_THRESHOLD 60.0   // Percentage

WebServer server(80);
DHT dht(DHTPIN, DHTTYPE);
SSD1306Wire display(OLED_ADDR, SDA_PIN, SCL_PIN);

// WiFi config - Mobile hotspot recommended
const char* ssid = "***";
const char* password = "***";

float currentTemperature = 0;
float currentHumidity = 0;
bool alarmStatus = false;
String alarmMessage = "";

const int led = 13;

void disableMotors() {
  pinMode(DRV_PWMA, OUTPUT);
  pinMode(DRV_PWMB, OUTPUT);
  pinMode(DRV_A2, OUTPUT);
  pinMode(DRV_B1, OUTPUT);
  pinMode(DRV_B2, OUTPUT);
  digitalWrite(DRV_PWMA, LOW);
  digitalWrite(DRV_PWMB, LOW);
  digitalWrite(DRV_A2, LOW);
  digitalWrite(DRV_B1, LOW);
  digitalWrite(DRV_B2, LOW);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  disableMotors();  // prevent motor spin from shared DHT/AIN1 pin

  dht.begin();  // Initiate DHT sensor

  pinMode(led, OUTPUT);

  ledcSetup(BUZZER_CHANNEL, 2000, 8); // 2KHz frequency, 8-bits
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

  display.init(); // initiate the OLED display
  // Vertical screen rotation display (select whether to enable based on the actual installation orientation)
  display.flipScreenVertically();
  // default font ArialMT_Plain_16
  display.setFont(ArialMT_Plain_16);
  // text align to left
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  // Connect to WiFi
  Serial.print("Attempting to connect to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int timeoutCounter = 0;

  // Stop if connected OR if 15 seconds have passed
  while (WiFi.status() != WL_CONNECTED && timeoutCounter < 15) {
    delay(1000);
    Serial.print(".");
    timeoutCounter++;
  }

  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Connection Timeout! Entering Offline Mode.");
  }

  delay(3000);

  // Define what the server does when the root directory "/" is accessed
  handleRoot();

  server.begin(); // Start the Web Server
  Serial.println("Set up HTTP server");

  Serial.println("System Startup");
  server.onNotFound(handleNotFound);
}

void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // update global variables
  currentTemperature = temperature;
  currentHumidity = humidity;

  // if failed to read the sensor
  if (isnan(humidity)) {
    Serial.println("Failed to read Humidity!");
    delay(2000);
  }
  if (isnan(temperature)) {
    Serial.println("Failed to read Temperature!");
    delay(2000);
    return;
  }

  // output information in serial monitor
  Serial.print("");
  Serial.print(temperature, 1);
  Serial.print(" °C | ");
  Serial.print(humidity, 1);
  Serial.println(" %RH");

  // refresh the OLED display
  displayData(temperature, humidity);

  // monitor thresholds
  if (temperature > TEMP_THRESHOLD || humidity > HUMI_THRESHOLD) {
    Serial.println("Warning");
    displayAlarm(); // show the warning text on the display
    alarmStatus = true;
    alarmMessage = "Warning";
    ledcWrite(BUZZER_CHANNEL, 128);  // trigger the alarm
  } else {
    alarmStatus = false;
    alarmMessage = "Normal";
    ledcWrite(BUZZER_CHANNEL, 0);    // stop the alarm
  }

  // delay 2000ms for each loop
  delay(2000);
}

void handleRoot() {
  server.on("/", []() {
    server.send(200, "text/html", "<h1>Hello! ESP32 is working!</h1>");
  });
}

void displayData(float temp, float hum) {
  // clear buffer
  display.clear();

  // display title
  display.setFont(ArialMT_Plain_10);
  display.drawString(10, 0, "Environmental testing");

  // show temperature
  display.setFont(ArialMT_Plain_16);
  String tempStr = String(temp, 1) + "°C";
  display.drawString(0, 15, tempStr);

  // show humidity
  String humStr = String(hum, 1) + "%";
  display.drawString(0, 36, humStr);

  // send to display
  display.display();
}

void displayAlarm() {
  display.setFont(ArialMT_Plain_16);
  display.drawString(70, 25, "ALARM!");
  display.display();
}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}
