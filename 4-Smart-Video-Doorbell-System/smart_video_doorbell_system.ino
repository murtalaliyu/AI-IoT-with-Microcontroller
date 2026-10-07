#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>

// Camera pin settings
#define PWDN_GPIO_NUM         -1
#define RESET_GPIO_NUM        -1
#define XCLK_GPIO_NUM         10
#define SIOD_GPIO_NUM         40
#define SIOC_GPIO_NUM         39
#define Y9_GPIO_NUM           48
#define Y8_GPIO_NUM           11
#define Y7_GPIO_NUM           12
#define Y6_GPIO_NUM           14
#define Y5_GPIO_NUM           16
#define Y4_GPIO_NUM           18
#define Y3_GPIO_NUM           17
#define Y2_GPIO_NUM           15
#define VSYNC_GPIO_NUM        38
#define HREF_GPIO_NUM         47
#define PCLK_GPIO_NUM         13

// Ultrasonic pin definitions
#define TRIG_PIN              4
#define ECHO_PIN              3

// Buzzer pin definitions
#define BUZZER_PIN            9

// Parameter configuration
const float SOUND_SPEED_CM_PER_US = 0.0343;
const unsigned long MEASUREMENT_TIMEOUT = 25000UL;
const float DETECTION_DISTANCE = 50.0;  // Trigger distance threshold (cm)
const unsigned long CAPTURE_INTERVAL = 10000;   // Photo capture interval during continuous detection (ms)

// Buzzer parameters
const int BUZZER_PWM_VALUE = 1;   // PWM value (0-255), controls buzzer volume
const unsigned long BUZZER_ON_TIME = 1000;  // Buzzer ring duration per cycle (ms)
const unsigned long BUZZER_OFF_TIME = 1000; // Buzzer mute duration per cycle (ms)

// WiFi configuration
const char* ssid = "***";
const char* password = "***";

// Global variables
WebServer server(80);
bool objectDetected = false;
unsigned long lastCaptureTime = 0;
unsigned long lastBuzzerTime = 0;
bool buzzerState = false;
String lastPhotoInfo = "Waiting...";

// Function declarations
bool initCamera();
void handleRoot();
void handleCapture();
void handlePhoto();
float measureDistance();
bool savePhotoToSPIFFS(camera_fb_t* fb);
void deleteOldPhoto();
void updateBuzzer();

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting the ultrasonic rangefinder doorbell...");

  // Initialize SPIFFS file system
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Initialization Failed!");
    return;
  }
  Serial.println("SPIFFS Initialization Successful");

  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera Initialization Failed, restarting...");
    delay(1000);
    ESP.restart();
  }
  Serial.println("Camera Initialization Successful");

  // Initialize ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("Ultrasonic sensor initialization successful");

  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, 0);   // initially turn off buzzer
  Serial.println("Buzzer initialization successful");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connection Successful");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Set Web server routing
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/photo", handlePhoto);

  // Start web server
  server.begin();
  Serial.println("The HTTP server has been initialized");

  // Clear old photos
  deleteOldPhoto();

  Serial.println("System startup complete, monitoring in progress...");
  Serial.println("Detection distance threshold: 50cm");
  Serial.println("Buzzer alert tone is enabled");
}

void loop() {
  server.handleClient();

  // Measure distance
  float distance = measureDistance();
  
  // Process distance measurement results
  if (distance > 0 && distance <= DETECTION_DISTANCE) {
    // Object is within detection range
    if (!objectDetected) {
      // Object detected for the first time
      objectDetected = true;
      lastCaptureTime = millis();
      lastBuzzerTime = millis();
      captureAndSavePhoto();
      Serial.printf("Object detected! Distance: %.2f cm - Taking Photo\n", distance);
      Serial.println("The buzzer begins to sound.");
    } else {
      // Object remains in range, check if interval photo is needed
      unsigned long currentTime = millis();
      if (currentTime - lastCaptureTime  >= CAPTURE_INTERVAL) {
        captureAndSavePhoto();
        lastCaptureTime = currentTime;
        Serial.printf("Object detecting! Distance: %.2f cm - Taking Photo\n", distance);
      }
    }
    // Control buzzer alert tome
    updateBuzzer();
  } else if (objectDetected) {
    // Object leaves detection range
    objectDetected = false;
    buzzerState = false;
    analogWrite(BUZZER_PIN, 0); // turn off buzzer
    Serial.println("The object has left the detection range; the buzzer is off.");
    if (distance > 0) {
      Serial.printf("Distance: %.2f cm\n", distance);
    }
  }

  // Display status once per second
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime >= 1000) {
    lastStatusTime = millis();
    if (distance > 0) {
      Serial.printf("Monitoring in progress... Distance: %.2f cm", distance);
      if (objectDetected) {
        Serial.print(" (The object is within range)");
      }
      Serial.println();
    } else if (distance == -1) {
      Serial.println("Distance: Beyond measurement range");
    } else if (distance == -2) {
      Serial.println("Distance: Sensor Failure");
    }
  }

  delay(100); // Main loop delay
}

// Update buzzer status
void updateBuzzer() {
  unsigned long currentTime = millis();
  if (buzzerState) {
    // Buzzer is ringing, check if it needs to be turned off
    if (currentTime - lastBuzzerTime >= BUZZER_ON_TIME) {
      buzzerState = false;
      lastBuzzerTime = currentTime;
      analogWrite(BUZZER_PIN, 0);   // turn off buzzer
      Serial.println("Buzzer OFF");
    }
  } else {
    // Buzzer is off, check if it needs to be turned on
    if (currentTime - lastBuzzerTime >= BUZZER_OFF_TIME) {
      buzzerState = true;
      lastBuzzerTime = currentTime;
      analogWrite(BUZZER_PIN, BUZZER_PWM_VALUE);  // turn on buzzer
      Serial.println("Buzzer ON");
    }
  }
}

// Initialize camera
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera initialization failed. 0x%x", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_framesize(s, FRAMESIZE_VGA);
  }

  return true;
}

// Measure distance
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, MEASUREMENT_TIMEOUT);

  if (duration == 0) {
    return -1.0;  // Timeout (distance too far)
  }

  if (duration < 10) {
    return -2.0;  // Invalid signal
  }

  float distance_cm = duration * SOUND_SPEED_CM_PER_US / 2.0;
  return distance_cm;
}

// Capture and save photo
void captureAndSavePhoto() {
  Serial.println("Start taking photos...");

  // Discard old frames to ensure getting the latest photo
  camera_fb_t *discard_fb = esp_camera_fb_get();
  if (discard_fb) {
    esp_camera_fb_return(discard_fb);
    delay(50);
  }

  // Get new photo
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Photo capture failed");
    return;
  }

  Serial.printf("Photo captured successfully, size: %d bytes\n", fb->len);

  // Save photo to SPIFFS
  if (savePhotoToSPIFFS(fb)) {
    // Update photo info
    char info[100];
    snprintf(info, sizeof(info), "Last photo: %lu, size: %d bytes", millis(), fb->len);
    lastPhotoInfo = String(info);
    Serial.println("Photo saved successfully");
  } else {
    Serial.println("Photo failed to save");
  }

  esp_camera_fb_return(fb);
}

// Save photo to SPIFFS
bool savePhotoToSPIFFS(camera_fb_t* fb) {
  // Delete old photo
  deleteOldPhoto();

  // Create new photo file
  File file = SPIFFS.open("/last_photo.jpg", FILE_WRITE);
  if (!file) {
    Serial.println("Cannot create new file.");
    return false;
  }

  // Write photo data
  if (file.write(fb->buf, fb->len) != fb->len) {
    Serial.println("Failed to write.");
    file.close();
    return false;
  }

  file.close();
  return true;
}

// Delete old photo
void deleteOldPhoto() {
  if (SPIFFS.exists("/last_photo.jpg")) {
    SPIFFS.remove("/last_photo.jpg");
    Serial.println("Photo deleted");
  }
}

// Handle root path request
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
      <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Ultrasonic Distance Measuring Doorbell</title>

        <style>
          body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin: 20px;
            background-color: #f0f0f0;
          }
          .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            padding: 30px;
            border-radius: 15px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.1);
          }
          h1 {
            color: #2c3e50;
            margin-bottom: 30px;
          }
          button {
            background-color: #3498db;
            border: none;
            color: white;
            padding: 15px 40px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 18px;
            margin: 15px;
            cursor: pointer;
            border-radius: 8px;
            transition: background-color 0.3s;
            font-weight: bold;
          }
          button:hover {
            background-color: #2980b9;
          }
          button:disabled {
            background-color: #95a5a6;
            cursor: not-allowed;
          }
          .info-box {
            background-color: #f8f9fa;
            border: 2px solid #e9ecef;
            border-radius: 10px;
            padding: 20px;
            margin: 25px 0;
            text-align: left;
          }
          .info-title {
            font-size: 18px;
            color: #495057;
            font-weight: bold;
            margin-bottom: 10px;
          }
          .info-content {
            font-size: 16px;
            color: #6c757d;
            line-height: 1.6;
          }
          .photo-frame {
            border: 3px solid #dee2e6;
            border-radius: 10px;
            padding: 15px;
            margin: 25px 0;
            background-color: #f8f9fa;
            min-height: 300px;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
          }
          #capturedImage {
            max-width: 90%;
            max-height: 400px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
          }
          .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
          }
          .status-active {
            background-color: #2ecc71;
          }
          .status-inactive {
            background-color: #e74c3c;
          }
          .system-status {
            font-size: 16px;
            color: #34495e;
            margin: 20px 0;
          }
        </style>
      </head>
        
      <body>
        <div class="container">
          <h1>Ultrasonic Distance Measuring Doorbell</h1>

          <div class="info-box">
            <div class="info-title">System Information</div>
            <div class="info-content">
              <p><strong>IP Address:</strong> )rawliteral" + String(WiFi.localIP()) + R"rawliteral(</p>
              <p><strong>Detection Distance:</strong> 50 cm</p>
              <p><strong>Detection status:</strong> <span id="detectionStatusText">Waiting for an object to enter the range</span></p>
              <p><strong>Buzzer:</strong> <span id="buzzerStatusText">Standby</span></p>
            </div>
          </div>

          <div class="info-box">
            <div class="info-title">Photo Information</div>
            <div class="info-content" id="photoInfo">
              )rawliteral" + lastPhotoInfo + R"rawliteral(
            </div>
          </div>

          <div style="margin: 30px 0;">
            <button onclick="window.location.href='/capture'">Manual Capture</button>
            <button onclick="window.location.href='/photo'">View Latest Photo</button>
            <button onclick="updatePhotoInfo()">Refresh Photo Info</button>
          </div>

          <div class="photo-frame" id="photoContainer">
            <p style="color: #868e96; font-style: italic;">Click the "View Latest Photos" button to display the photos taken."</p>
            <img id="capturedImage" src="" alt="Latest photos" style="display:none;">
          </div>

          <div class="system-status">
            <span class="status-indicator status-active"></span>
            System in operation - automatically takes a picture and sounds an alert when an object is detected.
          </div>
        </div>

        <script>
          // Update photo information
          function updatePhotoInfo() {
            // Logic to fetch the latest photo information
            document.getElementById('photoInfo').innerHTML = 'The information has been updated; the latest photos have been added.';
            setTimeout(() => {
              window.location.reload();
            }, 1000);
          }

          // Display photo
          function showPhoto() {
            document.getElementById('capturedImage').style.display = 'block';
            document.querySelector('.photo-frame p').style.display = 'none';
          }

          // Initialization on page load
          window.onload = function() {
            console.log('The doorbell system is ready.');
            // Check if the URL contains the photo parameter
            if (window.location.pathname === '/photo') {
              showPhoto();
            }
          };
        </script>
      </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Handle manual capture request
void handleCapture() {
  captureAndSavePhoto();

  // Return to the main HTML page, display a success message
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='3;url=/' />";
  html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;}</style>";
  html += "</head><body>";
  html += "<h2>✅ Photo taken successfully!</h2>";
  html += "<p>Return to homepage in 3 seconds...</p>";
  html += "<a href='/'>Return to homepage</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Handle view photo request
void handlePhoto() {
  if (SPIFFS.exists("/last_photo.jpg")) {
    File file = SPIFFS.open("/last_photo.jpg", FILE_READ);
    if (file) {
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.sendHeader("Pragma", "no-cache");
      server.sendHeader("Expires", "0");
      server.setContentLength(file.size());
      server.send(200, "image/jpeg", "");

      WiFiClient client = server.client();
      while (file.available()) {
        client.write(file.read());
      }
      file.close();
      return;
    }
  }

  // If there are no photos, display a prompt message
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='3;url=/' />";
  html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;}</style>";
  html += "</head><body>";
  html += "<h2>No Photo Available</h2>";
  html += "<p>No photos have been taken yet. Please wait for the object to enter the detection range or take a photo manually.</p>";
  html += "<p>Return to homepage in 3 seconds...</p>";
  html += "<a href='/'>Return to homepage</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}
