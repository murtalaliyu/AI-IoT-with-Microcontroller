const int trigPin = 4;
const int echoPin = 3;

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop() {
  // Clear the trig pin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Send a 10us pulse to trigger the 8-cycle 40KHz sonic burst
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // pulseIn() measures the duration of the high pulse on the echo pin
  long duration = pulseIn(echoPin, HIGH);

  // Calculate distance: speed of sound is 0.034 cm/us
  float distance = duration * 0.034 / 2;

  // Print results
  Serial.print("Current distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  // 500ms delay to allow residual waves to dissipate
  delay(500);
}
