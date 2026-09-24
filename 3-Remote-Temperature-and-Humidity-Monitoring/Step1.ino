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

  initDisplay();

  connectToWiFi();

  // Set Web server routing
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);

  server.begin(); // Start the Web Server
  Serial.println("HTTP server started!");
}

void loop() {
  // Handle client requests
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
  /*Serial.print("");
  Serial.print(temperature, 1);
  Serial.print(" °C | ");
  Serial.print(humidity, 1);
  Serial.println(" %RH");*/

  // refresh the OLED display
  displayData(temperature, humidity);

  // monitor thresholds
  if (temperature > TEMP_THRESHOLD || humidity > HUMI_THRESHOLD) {
    //Serial.println("Warning");
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

void initDisplay() {
  display.init(); // initiate the OLED display
  // Vertical screen rotation display (select whether to enable based on the actual installation orientation)
  display.flipScreenVertically();
  // default font ArialMT_Plain_16
  display.setFont(ArialMT_Plain_16);
  // text align to left
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

void connectToWiFi() {
  display.clear();
  display.drawString(0, 0, "Connecting to WiFi...");
  display.display();

  // Connect to WiFi
  Serial.print("\nAttempting to connect to: ");
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
  display.clear();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Display IP address on OLED
    display.drawString(0, 0, "WiFi Connected");
    display.drawString(0, 15, "IP:");
    display.drawString(20, 15, WiFi.localIP().toString());
    display.display();
  } else {
    Serial.println("WiFi Connection Timeout! Entering Offline Mode.");
    display.drawString(0, 0, "Connection Timeout!");
    display.display();
  }

  delay(3000);
}

void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
      <head>
        <title>ESP32-S3 Environment Monitoring</title>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <style>
          * {
            box-sizing: border-box; /* ensure padding doesn't increase container width */
          }
          body {
            font-family: Arial, sans-serif;
            text-align: center;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            margin: 0;
            padding: 5px;
            color: white;
            height: 100vh;
            overflow: hidden; /* prevent overflow scrolling */
          }
          .container {
            max-width: 100%;
            max-width: 500px;
            margin: 0 auto;
            background: rgba(255,255,255,0.1);
            padding: 10px;
            border-radius: 15px;
            background-filter: blur(10px);
          }
          h1 {
            font-size: 1.4em; 
            margin: 5px 0;  /* reduce title height */
          }
          .data-card {
            background: rgba(255,255,255,0.2);
            margin: 8px 0;
            padding: 10px;
            border-radius: 10px;
          }
          h2 {
            font-size: 1.1em;
            margin: 2px 0;
          }
          .alarm {
            background: rgba(255,0,0,0.3);
            color: #ff6b6b;
            font-weight: bold;
          }
          .value {
            font-size: 1.8em;
            margin: 2px 0;
          }
          .unit {
            font-size: 0.8em;
            opacity: 0.8;
          }
          .last-update {
            margin-top: 5px;
            font-size: 0.9em;
            opacity: 0.7;
          }
          button {
            background: #4CAF50;
            color: white;
            border: none;
            padding: 8px 15px;
            border-radius: 5px;
            cursor: pointer;
            margin: 5px;
          }
          button:hover {
            background: #45a049;
          }
        </style>
      </head>

      <body>
        <div class="container">
          <h1>Environment Monitor</h1>

          <div class="data-card" id="tempCard">
            <h2>Temperature</h2>
            <div class="value" id="temperature">--</div>
            <div class="unit">°C</div>
          </div>

          <div class="data-card" id="humiCard">
            <h2>Humidity</h2>
            <div class="value" id="humidity">--</div>
            <div class="unit">%RH</div>
          </div>

          <div class="data-card" id="statusCard">
            <h2>System Condition</h2>
            <div id="alarmStatus">--</div>
            <div class="last-update" id="lastUpdate">Last Update: --</div>
          </div>

          <button onclick="refreshData()">Refresh Data</button>
          <button onclick="location.reload()">Reload Page</button>
        </div>

        <script>
          function updateData() {
            fetch('/data')
            .then(response => response.json())
            .then(data => {
              document.getElementById('temperature').textContent = data.temperature.toFixed(1);
              document.getElementById('humidity').textContent = data.humidity.toFixed(1);
              document.getElementById('alarmStatus').textContent = data.alarmMessage;
              document.getElementById('lastUpdate').textContent = 'Last Update: ' + new Date().toLocaleString();

              // Update status color
              const statusCard = document.getElementById('statusCard');
              const tempCard = document.getElementById('tempCard');
              const humiCard = document.getElementById('humiCard');

              if (data.alarmStatus) {
                statusCard.classList.add('alarm');
              } else {
                statusCard.classList.remove('alarm');
              }

              // Temperature warning
              if (data.temperature > )rawliteral" + String(TEMP_THRESHOLD) + R"rawliteral() {
                tempCard.classList.add('alarm');
              } else {
                tempCard.classList.remove('alarm');
              }

              // Humidity warning
              if (data.humidity > )rawliteral" + String(HUMI_THRESHOLD) + R"rawliteral() {
                humiCard.classList.add('alarm');
              } else {
                humiCard.classList.remove('alarm');
              }
            })
            .catch(error => {
              console.error('Failed to acquire data:', error);
            });
          }

          function refreshData() {
            updateData();
          }

          // Fetch data on page load
          document.addEventListener('DOMContentLoaded', function() {
            updateData();
            // Automatically update data every 3 seconds
            setInterval(updateData, 3000);
          });
        </script>
      </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Handle data API request
void handleData() {
  String json = "{";
  json += "\"temperature\":" + String(currentTemperature, 1);
  json += ",\"humidity\":" + String(currentHumidity, 1);
  json += ",\"alarmStatus\":" + String(alarmStatus ? "true" : "false");
  json += ",\"alarmMessage\":\"" + alarmMessage + "\"";
  json += ",\"tempThreshold\":" + String(TEMP_THRESHOLD, 1);
  json += ",\"humiThreshold\":" + String(HUMI_THRESHOLD, 1);
  json += "}";

  server.send(200, "application/json", json);
}

// Handle status info request
void handleStatus() {
  String message = "ESP32-S3 Environment Monitoring System\n";
  message += "Temperature: " + String(currentTemperature, 1) + "°C\n";
  message += "Humidity: " + String(currentHumidity, 1) + "%\n";
  message += "Condition: " + alarmMessage + "\n";
  message += "IP: " + WiFi.localIP().toString() + "\n";

  server.send(200, "text/plain", message);
}

// Stream data to OLED display
void displayData(float temp, float hum) {
  // clear buffer
  display.clear();

  // display title - 10 pixel font
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
