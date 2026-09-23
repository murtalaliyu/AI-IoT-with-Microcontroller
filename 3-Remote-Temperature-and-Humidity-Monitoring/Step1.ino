#include <WebServer.h>

WebServer server(80);

// WiFi config - Mobile hotspot recommended
const char* ssid = "iPhone";
const char* password = "ihavethebestbrother";

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

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
  server.on("/", []() {
    server.send(200, "text/html", "<h1>Hello! ESP32 is working!</h1>");
  });

  server.begin(); // Start the Web Server
  Serial.println("Set up HTTP server");

  Serial.println("System Startup");
}

void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();
}
