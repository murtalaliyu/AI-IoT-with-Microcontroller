#ifndef TB6612_MOTOR_H
#define TB6612_MOTOR_H

// Includes
#include <Arduino.h>

// TB6612FNG driver pins
#define DRV_PWMA  43
#define DRV_A1    2
#define DRV_A2    1
#define DRV_PWMB  44
#define DRV_B1    4
#define DRV_B2    3

// BCC_ESP32S3 class for the ESP32-S3-DevKitC-1 breakout board
class BCC_ESP32S3 {
  public:
    /** @brief Constructor for BCC_ESP32S3 class */
    BCC_ESP32S3();

    /**
     * @brief   Initialize the TB6612FNG motor driver pins
     * @return  None
     */
    void motor_init(void);

    /**
     * @brief   Control both motors with speed values
     * @param   motor_a Speed for motor A (-100 to 100, negative for reverse)
     * @param   motor_b Speed for motor B (-100 to 100, negative for reverse)
     * @return  None
     */
    void motor(int8_t motor_a, int8_t motor_b);

  private:
    /** PWM channel assignments */
    uint8_t _pwmChannelA;
    uint8_t _pwmChannelB;
};
#endif  // BCC_ESP32S3_H
