#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <DHT.h>

// ======================== WiFi Config ========================
const char* WIFI_SSID     = "Dialog 4G 018";
const char* WIFI_PASSWORD = "917b765B";

// ======================== Server Config ======================
const char* SERVER_HOST      = "192.168.8.135";  // Python backend IP
const int   SERVER_PORT      = 8080;
const char* SENSOR_ENDPOINT  = "/api/sensor-data";
const char* AUDIO_ENDPOINT   = "/api/audio";

// ======================== DHT22 Config =======================
#define DPIN  5
#define DTYPE DHT22
DHT dht(DPIN, DTYPE);

// ======================== PIR & LDR Config ===================
#define PIRPIN 18
#define LDRPIN 19

// ======================== INMP441 I2S Config =================
#define I2S_WS   25
#define I2S_SCK  26
#define I2S_SD   33
#define I2S_PORT I2S_NUM_0

// Audio settings
#define SAMPLE_RATE     16000
#define SAMPLE_BITS     16
#define CHANNELS        1
#define AUDIO_BUFFER_SIZE 1024          // samples per read
#define AUDIO_SEND_SECONDS 3            // seconds of audio per chunk
#define AUDIO_CHUNK_BYTES (SAMPLE_RATE * (SAMPLE_BITS / 8) * AUDIO_SEND_SECONDS)

// WAV header size
#define WAV_HEADER_SIZE 44

// ======================== Timing =============================
unsigned long lastSensorSend  = 0;
unsigned long sensorInterval  = 2000;   // send sensor data every 2s
unsigned long lastAudioSend   = 0;
unsigned long audioInterval   = 4000;   // send audio every 4s

// ======================== Buffers ============================
int16_t i2sReadBuffer[AUDIO_BUFFER_SIZE];

// ======================== I2S Setup ==========================
void i2sInit() {
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = AUDIO_BUFFER_SIZE,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println("[I2S] INMP441 microphone initialized.");
}

// ======================== WAV Header =========================
void writeWavHeader(uint8_t* buffer, int dataSize) {
  int fileSize = dataSize + WAV_HEADER_SIZE - 8;
  int byteRate = SAMPLE_RATE * CHANNELS * (SAMPLE_BITS / 8);
  int blockAlign = CHANNELS * (SAMPLE_BITS / 8);

  // RIFF header
  buffer[0] = 'R'; buffer[1] = 'I'; buffer[2] = 'F'; buffer[3] = 'F';
  buffer[4] = fileSize & 0xFF;
  buffer[5] = (fileSize >> 8) & 0xFF;
  buffer[6] = (fileSize >> 16) & 0xFF;
  buffer[7] = (fileSize >> 24) & 0xFF;
  buffer[8] = 'W'; buffer[9] = 'A'; buffer[10] = 'V'; buffer[11] = 'E';

  // fmt subchunk
  buffer[12] = 'f'; buffer[13] = 'm'; buffer[14] = 't'; buffer[15] = ' ';
  buffer[16] = 16; buffer[17] = 0; buffer[18] = 0; buffer[19] = 0;
  buffer[20] = 1; buffer[21] = 0;  // PCM format
  buffer[22] = CHANNELS; buffer[23] = 0;
  buffer[24] = SAMPLE_RATE & 0xFF;
  buffer[25] = (SAMPLE_RATE >> 8) & 0xFF;
  buffer[26] = (SAMPLE_RATE >> 16) & 0xFF;
  buffer[27] = (SAMPLE_RATE >> 24) & 0xFF;
  buffer[28] = byteRate & 0xFF;
  buffer[29] = (byteRate >> 8) & 0xFF;
  buffer[30] = (byteRate >> 16) & 0xFF;
  buffer[31] = (byteRate >> 24) & 0xFF;
  buffer[32] = blockAlign; buffer[33] = 0;
  buffer[34] = SAMPLE_BITS; buffer[35] = 0;

  // data subchunk
  buffer[36] = 'd'; buffer[37] = 'a'; buffer[38] = 't'; buffer[39] = 'a';
  buffer[40] = dataSize & 0xFF;
  buffer[41] = (dataSize >> 8) & 0xFF;
  buffer[42] = (dataSize >> 16) & 0xFF;
  buffer[43] = (dataSize >> 24) & 0xFF;
}

// ======================== WiFi Connect =======================
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WiFi] Connection FAILED. Will retry in loop.");
  }
}

// ======================== Send Sensor Data ===================
void sendSensorData() {
  float hu = dht.readHumidity();
  float tc = dht.readTemperature();
  int motion = digitalRead(PIRPIN);
  int lightState = digitalRead(LDRPIN);

  if (isnan(tc) || isnan(hu)) {
    Serial.println("[Sensor] Failed to read DHT sensor!");
    return;
  }

  Serial.printf("[Sensor] Temp: %.1f C | Hum: %.1f%% | Motion: %s | Light: %s\n",
                tc, hu,
                motion == HIGH ? "YES" : "NO",
                lightState == HIGH ? "DARK" : "BRIGHT");

  // Send JSON via HTTP POST
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + SENSOR_ENDPOINT;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"temperature\":" + String(tc, 1) + ",";
    json += "\"humidity\":" + String(hu, 1) + ",";
    json += "\"motion\":" + String(motion == HIGH ? "true" : "false") + ",";
    json += "\"light_dark\":" + String(lightState == HIGH ? "true" : "false");
    json += "}";

    int httpCode = http.POST(json);
    if (httpCode > 0) {
      Serial.printf("[HTTP] Sensor data sent. Response: %d\n", httpCode);
    } else {
      Serial.printf("[HTTP] Sensor send failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

// ======================== Read & Send Audio ==================
void readAndSendAudio() {
  int totalBytes = WAV_HEADER_SIZE + AUDIO_CHUNK_BYTES;
  uint8_t* wavBuffer = (uint8_t*)malloc(totalBytes);
  if (!wavBuffer) {
    Serial.println("[Audio] Failed to allocate WAV buffer!");
    return;
  }

  int bytesCollected = 0;
  uint8_t* pcmStart = wavBuffer + WAV_HEADER_SIZE;

  Serial.printf("[Audio] Recording %d seconds of audio...\n", AUDIO_SEND_SECONDS);

  while (bytesCollected < AUDIO_CHUNK_BYTES) {
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_PORT, i2sReadBuffer,
                                 sizeof(i2sReadBuffer), &bytesRead,
                                 portMAX_DELAY);

    if (result == ESP_OK && bytesRead > 0) {
      int remaining = AUDIO_CHUNK_BYTES - bytesCollected;
      int toCopy = (bytesRead < remaining) ? bytesRead : remaining;
      memcpy(pcmStart + bytesCollected, i2sReadBuffer, toCopy);
      bytesCollected += toCopy;
    }
  }

  writeWavHeader(wavBuffer, AUDIO_CHUNK_BYTES);

  Serial.printf("[Audio] Captured %d bytes. Sending to server...\n", totalBytes);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + AUDIO_ENDPOINT;
    http.begin(url);
    http.addHeader("Content-Type", "audio/wav");
    http.setTimeout(10000);

    int httpCode = http.POST(wavBuffer, totalBytes);
    if (httpCode > 0) {
      String response = http.getString();
      Serial.printf("[HTTP] Audio sent. Response(%d): %s\n", httpCode, response.c_str());
    } else {
      Serial.printf("[HTTP] Audio send failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  free(wavBuffer);
}

// ======================== Setup ==============================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("   CryGuard ESP32 - Starting Up");
  Serial.println("========================================");

  // Initialize sensors
  dht.begin();
  pinMode(PIRPIN, INPUT);
  pinMode(LDRPIN, INPUT);
  Serial.println("[Sensor] DHT22, PIR, LDR initialized.");

  // Initialize I2S microphone
  i2sInit();

  // Connect WiFi
  connectWiFi();

  delay(2000);
  Serial.println("[System] Setup complete. Entering main loop.\n");
}

// ======================== Loop ===============================
void loop() {
  unsigned long now = millis();

  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected. Reconnecting...");
    connectWiFi();
  }

  // Send sensor data periodically
  if (now - lastSensorSend >= sensorInterval) {
    lastSensorSend = now;
    sendSensorData();
  }

  // Record and send audio periodically
  if (now - lastAudioSend >= audioInterval) {
    lastAudioSend = now;
    readAndSendAudio();
  }

  delay(100);
}
