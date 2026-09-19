// Includes
#include "tb6612_motor.h"
#include <Arduino.h>

// Constructor
BCC_ESP32S3::BCC_ESP32S3() {
  // Initialize PWM channels
  _pwmChannelA = 0;
  _pwmChannelB = 1;
}

// Public methods

/**
 * @brief Initialize the TB6612FNG motor driver pins
*/
void BCC_ESP32S3::motor_init(void) {
  // Configure motor driver pins
  pinMode(DRV_PWMA, OUTPUT);
  pinMode(DRV_A1, OUTPUT);
  pinMode(DRV_A2, OUTPUT);
  pinMode(DRV_PWMB, OUTPUT);
  pinMode(DRV_B1, OUTPUT);
  pinMode(DRV_B2, OUTPUT);

  // Set up PWM for motor control
  ledcSetup(_pwmChannelA, 20000, 8);  // 20KHz, 8-bit resolution
  ledcSetup(_pwmChannelB, 20000, 8);  // 20KHz, 8-bit resolution
  ledcAttachPin(DRV_PWMA, _pwmChannelA);
  ledcAttachPin(DRV_PWMB, _pwmChannelB);

  // Initialize motors to stopped state
  digitalWrite(DRV_A1, LOW);
  digitalWrite(DRV_A2, LOW);
  digitalWrite(DRV_B1, LOW);
  digitalWrite(DRV_B2, LOW);
  ledcWrite(_pwmChannelA, 0);
  ledcWrite(_pwmChannelB, 0);
}

/**
 * @brief Control both motors with speed values
*/
void BCC_ESP32S3::motor(int8_t motor_a, int8_t motor_b) {
  // Limit input values to -100 to 100 range
  constrain(motor_a, -100, 100);
  constrain(motor_b, -100, 100);

  // Motor A control
  if (motor_a > 0) {
    // Forward
    digitalWrite(DRV_A1, HIGH);
    digitalWrite(DRV_A2, LOW);
    ledcWrite(_pwmChannelA, map(motor_a, 0, 100, 0, 255));
  }
  else if (motor_a < 0) {
    // reverse
    digitalWrite(DRV_A1, LOW);
    digitalWrite(DRV_A2, HIGH);
    ledcWrite(_pwmChannelA, map(abs(motor_a), 0, 100, 0, 255));
  }
  else {
    // Stop
    digitalWrite(DRV_A1, LOW);
    digitalWrite(DRV_A2, LOW);
    ledcWrite(_pwmChannelA, 0);
  }

  // Motor B control
  if (motor_b > 0) {
    // Forward
    digitalWrite(DRV_B1, HIGH);
    digitalWrite(DRV_B2, LOW);
    ledcWrite(_pwmChannelB, map(motor_b, 0, 100, 0, 255));
  }
  else if (motor_b < 0) {
    // Reverse
    digitalWrite(DRV_B1, LOW);
    digitalWrite(DRV_B2, HIGH);
    ledcWrite(_pwmChannelB, map(abs(motor_b), 0, 100, 0, 255));
  }
  else {
    // Stop
    digitalWrite(DRV_B1, LOW);
    digitalWrite(DRV_B2, LOW);
    ledcWrite(_pwmChannelB, 0);
  }
}
