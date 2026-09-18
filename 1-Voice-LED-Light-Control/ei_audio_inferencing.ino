/* Edge Impulse Arduino examples
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// These sketches are tested with 2.0.4 ESP32 Arduino Core
// https://github.com/espressif/arduino-esp32/releases/tag/2.0.4

// If your target is limited in memory remove this macro to save 10K RAM
#define EIDSP_QUANTIZE_FILTERBANK   0

/*
 ** NOTE: If you run into TFLite arena allocation issue.
 **
 ** This may be due to may dynamic memory fragmentation.
 ** Try defining "-DEI_CLASSIFIER_ALLOCATION_STATIC" in boards.local.txt (create
 ** if it doesn't exist) and copy this file to
 ** `<ARDUINO_CORE_INSTALL_PATH>/arduino/hardware/<mbed_core>/<core_version>/`.
 **
 ** See
 ** (https://support.arduino.cc/hc/en-us/articles/360012076960-Where-are-the-installed-cores-located-)
 ** to find where Arduino installs cores on your machine.
 **
 ** If the problem persists then there's not enough memory for this model and application.
 */

 // Reference: https://wiki.seeedstudio.com/xiao_esp32s3_keyword_spotting/#step-6-deploying-to-xiao-esp32s3-sense

/* Includes ---------------------------------------------------------------- */
#include <Voice_LED_Light_Control_inferencing.h>

#include <Adafruit_NeoPixel.h>  // Add WS2812 library

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s.h"

// WS2812 configuration
#define PIN_NEOPIXEL 8  // Control pin
#define NUMPIXELS 10    // Number of LEDs

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Light color definitions
#define RED pixels.Color(255, 0, 0)         // Red
#define GREEN pixels.Color(0, 255, 0)       // Green
#define BLUE pixels.Color(0, 0, 255)        // Blue
#define YELLOW pixels.Color(255, 255, 0)    // Yellow
#define OFF pixels.Color(0, 0, 0)           // Off

/** Audio buffers, pointers and selectors */
typedef struct {
    signed short *buffers[2];
    unsigned char buf_select;
    unsigned char buf_ready;
    unsigned int buf_count;
    unsigned int n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
static bool record_status = true;

// Store current predition result
static ei_impulse_result_t current_result;

/**
 * @brief       Control WS2812 lights
 * @param[in]   command Voice command type
*/
void controlLED(const String& command) {
    if (command == "green") {
        pixels.fill(GREEN);
        pixels.show();
        ei_printf("LED: Green ON\n");
    }
    else if (command == "turn_off") {
        pixels.fill(OFF);
        pixels.show();
        ei_printf("LED: OFF\n");
    }
    else if (command == "yellow") {
        pixels.fill(YELLOW);
        pixels.show();
        ei_printf("LED: Yello ON\n");
    }
    else if (command == "red") {
        pixels.fill(RED);
        pixels.show();
        ei_printf("Red ON\n");
    }
    // No action for Noise
    else if (command == "noise") {
        ei_printf("Noise detected, no action\n");
    }
    else if (command == "others") {
        ei_printf("Others detected, no action\n");
    }
}

/**
 * @brief       Get the label with the highest similarity
 * @param[in]   result Recognition result
 * @return      Label string
*/
String getHighestPrediction(const ei_impulse_result_t& result) {
    float max_value = 0;
    int max_index = 0;
    float noise_value = 0;

    // Iterate through all classification results
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        float current_score = result.classification[ix].value;
        String current_label = String(result.classification[ix].label);

        // Record the probability of noise classification
        if (current_label == "noise") {
            noise_value = current_score;
        }

        // Find the classification with the highest probability
        if (current_score > max_value) {
            max_value = current_score;
            max_index = ix;
        }
    }

    // Execute logical judgement: max probability > 0.6 and noise probability < 0.3
    if (max_value > 0.6 && noise_value < 0.3) {
        // If conditions are met, return the actual recognized highest probability label
        return String(result.classification[max_index].label);
    }else {
        // If conditions are not met, return "noise" so controlLED ignores this operation
        return String("noise");
    }
}

/**
 * @brief      Arduino setup function
 */
void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    // comment out the below line to cancel the wait for USB connection (needed for native USB)
    while (!Serial);
    Serial.println("Edge Impulse Inferencing Demo with WS2812 Control");

    // initialize WS2812
    pixels.begin();
    pixels.setBrightness(20);
    pixels.fill(RED);
    // turn off all LED lights
    pixels.show();

    // summary of inferencing settings (from model_metadata.h)
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: ");
    ei_printf_float((float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf(" ms.\n");
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    run_classifier_init();
    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);

    if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
        ei_printf("ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        return;
    }

    ei_printf("Recording...\n");
}

/**
 * @brief      Arduino main function. Runs the inferencing loop.
 */
void loop()
{
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
        // print the predictions
        ei_printf("Predictions ");
        ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
            result.timing.dsp, result.timing.classification, result.timing.anomaly);
        ei_printf(": \n");
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("    %s: ", result.classification[ix].label);
            ei_printf_float(result.classification[ix].value);
            ei_printf("\n");
        }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ei_printf("    anomaly score: ");
        ei_printf_float(result.anomaly);
        ei_printf("\n");
#endif
        String highestPrediction = getHighestPrediction(result);
        ei_printf("Highest prediction: %s\n", highestPrediction.c_str());

        controlLED(highestPrediction);
        print_results = 0;
    }
}

static void audio_inference_callback(uint32_t n_bytes)
{
    for(int i = 0; i < n_bytes>>1; i++) {
        inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];

        if(inference.buf_count >= inference.n_samples) {
            inference.buf_select ^= 1;
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

static void capture_samples(void* arg) {

  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;

  while (record_status) {

    /* read data at once from i2s */
    i2s_read(I2S_NUM_0, (void*)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);
    if (bytes_read > 0) {
        for (int x = 0; x < i2s_bytes_to_read/2; x++) {
            sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
        }
        audio_inference_callback(i2s_bytes_to_read);
    }
  }
  vTaskDelete(NULL);
}

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));

    if (inference.buffers[0] == NULL) {
        return false;
    }

    inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));

    if (inference.buffers[1] == NULL) {
        ei_free(inference.buffers[0]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    if (i2s_init(EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Failed to start I2S! Setup aborted\n");
        return false;
    }

    ei_sleep(100);

    record_status = true;

    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void*)sample_buffer_size, 10, NULL);

    return true;
}

/**
 * @brief      Wait on new data
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void)
{
    bool ret = true;

    if (inference.buf_ready == 1) {
        ei_printf(
            "Error sample buffer overrun. Decrease the number of slices per model window "
            "(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)\n");
        ret = false;
    }

    while (inference.buf_ready == 0) {
        delay(1);
    }

    inference.buf_ready = 0;
    return true;
}

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);

    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
static void microphone_inference_end(void)
{
    i2s_deinit();
    ei_free(inference.buffers[0]);
    ei_free(inference.buffers[1]);
}


static int i2s_init(uint32_t sampling_rate) {
  // Start listening for audio: MONO @ 8/16KHz
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = sampling_rate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = -1,
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num = -1,    // IIS_SCLK
      .ws_io_num = 42,
      .data_out_num = -1,  // IIS_DSIN
      .data_in_num = 41,   // IIS_DOUT
  };
  esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (ret != ESP_OK) return (int)ret;

  return (int)i2s_set_pin(I2S_NUM_0, &pin_config);
}

static int i2s_deinit(void) {
    i2s_driver_uninstall((i2s_port_t)1); //stop & destroy i2s driver
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
