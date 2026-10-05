#include "esp_camera.h"
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>

// Camera pin settings (ESP32-S3 Sense)
#define PWDN_GPIO_NUM       -1
#define RESET_GPIO_NUM      -1
#define XCLK_GPIO_NUM       10
#define SIOD_GPIO_NUM       40
#define SIOC_GPIO_NUM       39
#define Y9_GPIO_NUM         48
#define Y8_GPIO_NUM         11
#define Y7_GPIO_NUM         12
#define Y6_GPIO_NUM         14
#define Y5_GPIO_NUM         16
#define Y4_GPIO_NUM         18
#define Y3_GPIO_NUM         17
#define Y2_GPIO_NUM         15
#define VSYNC_GPIO_NUM      38
#define HREF_GPIO_NUM       47
#define PCLK_GPIO_NUM       13

// WiFi configuration
const char* ssid = "***";
const char* password = "***";

// Declare Web server, port 80
WebServer server(80);

// Function declarations
bool initCamera();
void handleRoot();
void handleCapture();
void handlePhoto();
void captureAndSavePhoto();

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting simple camera server...");

  // 1. Initialize SPIFFS file system (alternative to SD card)
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    return;
  }

  // 2. Initialize camera
  if (!initCamera()) {
    Serial.println("Camera initialization failed!");
    return;
  }

  // 3. Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected successfully!");
  Serial.print("Please visit the IP address in your browser: ");
  Serial.println(WiFi.localIP());

  // 4. Set up Web server routing
  server.on("/", handleRoot);             // Access homepage
  server.on("/capture", handleCapture);   // Receive capture command
  server.on("/photo", handlePhoto);       // View photo

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  // Handle Web client requests
  server.handleClient();
}

void captureAndSavePhoto() {
  Serial.println("Starting to take photo...");

  // Discard old frames to ensure getting the latest photo
  camera_fb_t *discard_fb = esp_camera_fb_get();
  if (discard_fb) {
    esp_camera_fb_return(discard_fb);
    delay(50);
  }

  // Get new photo
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to take photo");
    return;
  }

  // Delete old photo in SPIFFS
  if (SPIFFS.exists("/last_photo.jpg")) {
    SPIFFS.remove("/last_photo.jpg");
    Serial.println("Old photo discarded");
  }

  // Write new photo to SPIFFS
  File file = SPIFFS.open("/last_photo.jpg", FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.printf("New photo recorded, size: %d bytes\n", fb->len);
  } else {
    Serial.println("File write failed");
  }

  // Release camera buffer
  esp_camera_fb_return(fb);
}

// Initialize camera configuration
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
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
  if (err != ESP_OK) return false;

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_vflip(s, 1);
  }

  return true;
}

// --- Web server route handling ---

// 1. Generate minimalist front-end page
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
      <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>ESP32 Simple Camera</title>
        <style>
          body {
            font-family: Arial;
            text-align: center;
            margin-top: 50px;
            background-color: #f0f0f0;
          }
          button {
            background-color: #3498db;
            color: white;
            border: none;
            padding: 15px 30px;
            font-size: 18px;
            border-radius: 8px;
            cursor: pointer;
            font-weight: bold;
          }
          button:hover {
            background-color: #2980b9;
          }
          img {
            max-width: 90%;
            max-height: 500px;
            margin-top: 30px;
            border-radius: 10px;
            box-shadow: 0 4px 10px rgba(0,0,0,0.2);
            display: none;
          }
        </style>
      </head>

      <body>
        <h1>ESP32 Simple Camera Server</h1>
        <button id="capBtn" onclick="takePhoto()">Click to record new photo</button>
        <br>
        <img id="photo" src="" alt="Captured Photo">

        <script>
          function takePhoto() {
            const btn = document.getElementById('capBtn');
            btn.innerText = "Capturing...";
            btn.disabled = true;

            // Request ESP32 to take a photo
            fetch('/capture')
            .then(response => {
              if (response.ok) {
                // After successful capture, reload image and add timestamp to avoid caching
                const img = document.getElementById('photo');
                img.src = '/photo?t=' + new Date().getTime();
                img.style.display = 'inline-block';
              }
            })
            .finally(() => {
              btn.innerText = "Click to record new photo";
              btn.disabled = false;
            });
          }
        </script>
      </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

// 2. Handle button request, call capture logic
void handleCapture() {
  captureAndSavePhoto();
  server.send(200, "text/plain", "OK");
}

// 3. Stream photo from SPIFFS to browser
void handlePhoto() {
  if (SPIFFS.exists("/last_photo.jpg")) {
    File file = SPIFFS.open("/last_photo.jpg", FILE_READ);
    if (file) {
      server.streamFile(file, "image/jpeg");
      file.close();
      return;
    }
  }
  server.send(404, "text/plain", "No Photo Available");
}
