/*
  WAV Recorder for Seeed XIAO ESP32S3 Sense
  Features: Serial Input Filename & Auto-increment Suffix

  Reference: https://wiki.seeedstudio.com/xiao_esp32s3_keyword_spotting/#step-6-deploying-to-xiao-esp32s3-sense
*/

#include <I2S.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Make changes as needed
#define RECORD_TIME 20  // seconds, max value is 240

// Do not change for best results
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 2

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // I2S Initialization
  I2S.setAllPins(-1, 42, 41, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, SAMPLE_BITS)) {
    Serial.println("Failed to initialize I2S!");
    while (1);
  }

  // SD Card Initialization
  if (!SD.begin(21)) {
    Serial.println("Failed to mount SD Card!");
    while (1);
  }

  Serial.println("\n-----------------------------");
  Serial.println("System Ready.");
  Serial.println("Please enter a filename in the Serial Monitor to start recording.");
  Serial.println("Example: 'test' (will save as /test.wav)");
  Serial.println("-----------------------------\n");
}

void loop() {
  // Check if user sent data via Serial
  if (Serial.available() > 0) {
    String inputName = Serial.readStringUntil('\n');
    inputName.trim(); // Remove newline or carriage return characters

    if (inputName.length() > 0) {
      String finalPath = getUniqueFilename(inputName);
      record_wav(finalPath);

      // Prompt for next recording
      Serial.println("\n-----------------------------");
      Serial.println("Enter a new filename to record again.");
      Serial.println("\n-----------------------------");
    }
  }
  delay(100);
}

// Helper function to handle filename duplication
String getUniqueFilename(String baseName) {
  String extension = ".wav";
  String fullPath = "/" + baseName + extension; // Ensure path starts with /

  // Check if file exists, if so, append _number
  int counter = 1;
  while (SD.exists(fullPath)) {
    fullPath = "/" + baseName + "_" + String(counter) + extension;
    counter++;
  }

  return fullPath;
}

void record_wav(String fileName) {
  uint32_t sample_size = 0;
  uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;
  uint8_t *rec_buffer = NULL;

  Serial.printf("Ready to start recording to: %s ...\n", fileName.c_str());

  File file = SD.open(fileName, FILE_WRITE);

  // Write the header to the WAV file
  uint8_t wav_header[WAV_HEADER_SIZE];
  generate_wav_header(wav_header, record_size, SAMPLE_RATE);
  file.write(wav_header, WAV_HEADER_SIZE);

  // PSRAM malloc for recording
  rec_buffer = (uint8_t *)ps_malloc(record_size);
  if (rec_buffer == NULL) {
    Serial.printf("malloc failed!\n");
    while (1);
  }

  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());

  // Start recording
  // Note: Using the specific esp_i2s namespace call from your original code
  esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, rec_buffer, record_size, &sample_size, portMAX_DELAY);

  if (sample_size == 0) {
    Serial.printf("Recording Failed!\n");
  } else {
    Serial.printf("Recorded %d bytes\n", sample_size);
  }

  // Increase volume
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
    (*(uint16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
  }

  // Write data to the WAV file
  Serial.printf("Writing to the file ...\n");
  if (file.write(rec_buffer, record_size) != record_size) {
    Serial.printf("Write file Failed!\n");
  }

  free(rec_buffer);
  file.close();
  Serial.printf("The recording is over. Saved as: %s\n", fileName.c_str());
}

void generate_wav_header(uint8_t *wav_header, uint32_t wav_size, uint32_t sample_rate) {
  uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
  uint32_t byte_rate = SAMPLE_RATE * SAMPLE_BITS / 8;

  const uint8_t set_wav_header[] = {
    'R', 'I', 'F', 'F', // ChunkID
    file_size, file_size >> 8, file_size >> 16, file_size >> 24,  // ChunkSize
    'W', 'A', 'V', 'E', // Format
    'f', 'm', 't', ' ', // Subchunk1ID
    0x10, 0x00, 0x00, 0x00, // Subchunk1Size (16 for PCM)
    0x01, 0x00, // AudioFormat (1 for PCM)
    0x01, 0x00, // NumChannels (1 channel)
    sample_rate, sample_rate >> 8, sample_rate >> 16, sample_rate >> 24,  // SampleRate
    byte_rate, byte_rate >> 8, byte_rate >> 16, byte_rate >> 24,  // ByteRate
    0x02, 0x00, // BlockAlign
    0x10, 0x00, // BitsPerSample (16 bits)
    'd', 'a', 't', 'a', // Subchunk2ID
    wav_size, wav_size >> 8, wav_size >> 16, wav_size >> 24,  // Subchunk2Size
  };

  memcpy(wav_header, set_wav_header, sizeof(set_wav_header));
}