/**
* This code is used by the on-board ESP32S3 to continuously capture
* images and drive the motors to follow the target.
*/

// Includes
#include <Image_Pattern_Recognition_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"

// TB6612 Motor drive
#include "tb6612_motor.h"

// Create motor object
BCC_ESP32S3 motor_control;

// Set motor speed
#define MOTOR_SPEED       -25
#define TURN_SPEED        -40

// Select camera model
#define XIAO_ESP32_S3_SENSE // has PSRAM

#if defined(XIAO_ESP32_S3_SENSE)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13
#else
#error "Camera model not selected"
#endif

// Constant defines
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE         3

// Private variables
static bool debug_nn = false;  // Set this to true to see e.g. features generated from the raw signal
static bool is_initialized = false;
uint8_t *snapshot_buf;  // points to the output of the capture

int missing_detection_count = 0;
const int MAX_MISSING_FRAMES = 2; // allow to continue moving for x frames

static camera_config_t camera_config = {
  .pin_pwdn = PWDN_GPIO_NUM,
  .pin_reset = RESET_GPIO_NUM,
  .pin_xclk = XCLK_GPIO_NUM,
  .pin_sscb_sda = SIOD_GPIO_NUM,
  .pin_sscb_scl = SIOC_GPIO_NUM,

  .pin_d7 = Y9_GPIO_NUM,
  .pin_d6 = Y8_GPIO_NUM,
  .pin_d5 = Y7_GPIO_NUM,
  .pin_d4 = Y6_GPIO_NUM,
  .pin_d3 = Y5_GPIO_NUM,
  .pin_d2 = Y4_GPIO_NUM,
  .pin_d1 = Y3_GPIO_NUM,
  .pin_d0 = Y2_GPIO_NUM,
  .pin_vsync = VSYNC_GPIO_NUM,
  .pin_href = HREF_GPIO_NUM,
  .pin_pclk = PCLK_GPIO_NUM,

  // XCLK 20MHz or 10MHz for 0V2640 double FPS (Experimental)
  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,

  .pixel_format = PIXFORMAT_JPEG, // YUV422, GRAYSCALE, RGB565, JPEG
  .frame_size = FRAMESIZE_QVGA,   // QQVGA-UXGA Do not use sizes above QVGA when not JPEG

  .jpeg_quality = 12,             // 0-63 lower number means higher quality
  .fb_count = 1,                  // if more than one, i2s runs in continuous mode. Use only with JPEG
  .fb_location = CAMERA_FB_IN_PSRAM,
  .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

// Function definitions
bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);

/**
* @brief  Arduino setup function
*/
void setup() {
  Serial.begin(921600);
  pinMode(21, OUTPUT);

  // comment out the line below to start inference immediately after upload
  digitalWrite(21, HIGH); // turn the LED on (HIGH is the voltage level)
  delay(1000); // wait for 1 second
  digitalWrite(21, LOW);
  delay(1000);
  digitalWrite(21, HIGH);
  delay(1000);

  Serial.println("Edge Impulse Inferencing");

  // initialize motor controller
  motor_control.motor_init();
  Serial.println("Motor controller initialized");

  digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  delay(1000);
  digitalWrite(21, HIGH);
  delay(1000);

  // set motor to stop
  motor_control.motor(0, 0);

  if (ei_camera_init() == false) {
    ei_printf("Failed to initialize Camera!\r\n");
  } else {
    ei_printf("Camera initialized\r\n");
  }

  // blink LED 4 times
  digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  delay(1000);digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  delay(1000);digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  delay(1000);digitalWrite(21, HIGH);
  delay(1000);
  digitalWrite(21, LOW);
  delay(1000);

  ei_printf("\nStarting continuous inference in 2 seconds...\n");
  ei_sleep(2000);
}

/**
* @brief      Get data and run inferencing
*
* @param[in]  debug Get debug info if true
*/
void loop() {
  // instead of wait_ms, we'll wait on the signal. this allows threads to cancel us...
  if (ei_sleep(5) != EI_IMPULSE_OK) {
    return;
  }
  snapshot_buf = (uint8_t *)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);

  // check if allocation was successful
  if (snapshot_buf == nullptr) {
    ei_printf("ERR: Failed to allocate snapshot buffer!\n");
    return;
  }

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &ei_camera_get_data;

  if (ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf) == false) {
    ei_printf("Failed to capture image\r\n");
    free(snapshot_buf);
    return;
  }

  // Run the classifier
  ei_impulse_result_t result = { 0 };

  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
  if (err != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\n", err);
    free(snapshot_buf);
    return;
  }

  // print the predictions
  ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
              result.timing.dsp, result.timing.classification, result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  ei_printf("Object detection bounding boxes:\r\n");
  bool target_detected = false;

  for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
    ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
    if (bb.value == 0) {
      continue;
    }

    Serial.printf("MARK:%u,%u,%u,%u,%s\n", bb.x, bb.y, bb.width, bb.height, bb.label);

    if (strcmp(bb.label, "wealth") == 0) {
      target_detected = true;

      // acquire coordinate and size
      uint32_t x = bb.x;
      uint32_t y = bb.y;
      uint32_t width = bb.width;
      uint32_t height = bb.height;
      uint32_t center_x = x + width / 2;

      Serial.printf(">>> Object detection <<<\n");
      Serial.printf("Coordinate: X=%u, Y=%u, CenterX=%u\n", x, y, center_x);
      Serial.printf("Size: %u%u px\n", width, height);
      Serial.printf("Confidence: %.2f\n", bb.value);

      // if object size greater than 40 pixels
      if (width > 40 || height > 40) {
        Serial.printf("*** Stop ***\n");
        motor_control.motor(0, 0);
      }

      // change direction
      else if (center_x > 68) {
        // turn left
        Serial.printf(">>> Turn left <<<\n");
        motor_control.motor(MOTOR_SPEED, TURN_SPEED);
        digitalWrite(21, LOW);
      } else if (center_x < 28) {
        // turn right
        Serial.printf(">>> Turn right <<<\n");
        motor_control.motor(TURN_SPEED, MOTOR_SPEED);
        digitalWrite(21, LOW);
      } else {
        // go straight
        Serial.printf(">>> Forward <<<\n");
        motor_control.motor(MOTOR_SPEED, MOTOR_SPEED);
        digitalWrite(21, LOW);
      }
      Serial.printf("=======================");
    }
  }

  // if no object detected, stop the motors
  if (!target_detected) {
    missing_detection_count++;
    if (missing_detection_count >= MAX_MISSING_FRAMES) {
      motor_control.motor(0, 0);
      if (missing_detection_count == MAX_MISSING_FRAMES) {
        Serial.printf("No object detected - Stopping\n");
      }
      digitalWrite(21, HIGH);
    }
  } else {
    missing_detection_count = 0;
  }

  // Print the prediction results (classification)
#else
  ei_printf("Predictions:\r\n");
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    ei_printf("  %s: ", ei_classifier_inferencing_categories[i]);
    ei_printf("%.5f\r\n", result.classification[i].value);
  }
#endif

  // Print anomaly result (if it exists)
#if EI_CLASSIFIER_HAS_ANOMALY
  ei_printf("Anomaly prediction: %.3f\r\n", result.anomaly);
#endif

#if EI_CLASSIFIER_HAS_VISUAL_ANOMALY
  ei_printf("Visual anomalies:\r\n");
  for (uint32_t i = 0; i < result.visual_ad_count; i++) {
    ei_impulse_result_bounding_box bb = result.visual_ad_grid_cells[i];
    if (bb.value == 0) {
      continue;
    }

    ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);
  }
#endif

  free(snapshot_buf);
}

/**
* @brief    Setup image sensor & start streaming
*
* @retval   false if initialization failed
*/
bool ei_camera_init(void) {
  if (is_initialized) return true;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // initilize the camera
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, 0);  // lower the saturation
  }

#if defined(CAMERA_MODEL_MSSTACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#elif defined(CAMERA_MODEL_ESP_EYE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
  s->set_awb_gain(s, 1);
#endif

  is_initialized = true;
  return true;
}

/**
* @brief  Stop streaming of sensor data
*/
void ei_camera_deinit(void) {
  // deinitialize the camera
  esp_err_t err = esp_camera_deinit();

  if (err != ESP_OK) {
    ei_printf("Camera deinit failed\n");
    return;
  }

  is_initialized = false;
  return;
}

/**
* @brief      Capture, rescale and crop image
*
* @param[in]  img_width   width of output image
* @param[in]  img_height  height of output image
* @param[in]  out_buf     pointer to store output image, NULL may be used
*                         if ei_camera_frame_buffer is to be used for capture and resize/cropping.
*
* @retval     false if not initialized, image captured, rescaled or cropped failed
*/
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
  bool do_resize = false;

  if (!is_initialized) {
    ei_printf("ERR: Camera is not initialized\r\n");
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    ei_printf("Camera capture failed\n");
    return false;
  }

  Serial.print("FRAME_START");
  Serial.write(fb->buf, fb->len);   // sending JPEG
  Serial.print("FRAME_END");

  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);

  esp_camera_fb_return(fb);

  if (!converted) {
    ei_printf("Conversion failed\n");
    return false;
  }

  if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
    do_resize = true;
  }

  if (do_resize) {
    ei::image::processing::crop_and_interpolate_rgb888(
      out_buf,
      EI_CAMERA_RAW_FRAME_BUFFER_COLS,
      EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
      out_buf,
      img_width,
      img_height
    );
  } else {
    memcpy(out_buf, snapshot_buf, img_width * img_height * 3);
  }

  return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  // we already have a RGB888 buffer, so recalculate offset into pixel index
  size_t pixel_ix = offset * 3;
  size_t pixels_left = length;
  size_t out_ptr_ix = 0;

  while (pixels_left != 0) {
    // Swap BGR to RGB here
    // due to https://github.com/espressif/esp32-camera/issues/379
    out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];

    // go to the next pixel
    out_ptr_ix++;
    pixel_ix += 3;
    pixels_left--;
  }

  return 0;
}
#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
