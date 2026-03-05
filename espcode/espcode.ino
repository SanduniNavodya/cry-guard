#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <DHT.h>

// ======================== WiFi Config ========================
const char* WIFI_SSID     = "Dulshan Dialog 4G 643";
const char* WIFI_PASSWORD = "Chuki000";

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
#define I2S_READ_SAMPLES 512               // samples per I2S read (32ms @ 16kHz)
#define I2S_READ_BYTES   (I2S_READ_SAMPLES * (SAMPLE_BITS / 8))
#define AUDIO_SEND_SECONDS 3               // seconds of audio for cry detection
#define AUDIO_CHUNK_BYTES  (SAMPLE_RATE * (SAMPLE_BITS / 8) * AUDIO_SEND_SECONDS)
#define WAV_HEADER_SIZE 44

// ======================== WebSocket Audio Stream ==============
WebSocketsClient webSocket;
bool wsConnected = false;

// ======================== Buffers ============================
int16_t i2sReadBuffer[I2S_READ_SAMPLES];

// Cry detection accumulation buffer (allocated in setup)
uint8_t* cryBuffer = NULL;
int cryBufferOffset = 0;

// ======================== Timing =============================
unsigned long lastSensorSend = 0;
unsigned long sensorInterval = 2000;      // send sensor data every 2s

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
    .dma_buf_len          = I2S_READ_SAMPLES,
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

  buffer[0]='R'; buffer[1]='I'; buffer[2]='F'; buffer[3]='F';
  buffer[4] = fileSize & 0xFF;
  buffer[5] = (fileSize >> 8) & 0xFF;
  buffer[6] = (fileSize >> 16) & 0xFF;
  buffer[7] = (fileSize >> 24) & 0xFF;
  buffer[8]='W'; buffer[9]='A'; buffer[10]='V'; buffer[11]='E';

  buffer[12]='f'; buffer[13]='m'; buffer[14]='t'; buffer[15]=' ';
  buffer[16]=16; buffer[17]=0; buffer[18]=0; buffer[19]=0;
  buffer[20]=1;  buffer[21]=0;
  buffer[22]=CHANNELS; buffer[23]=0;
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

  buffer[36]='d'; buffer[37]='a'; buffer[38]='t'; buffer[39]='a';
  buffer[40] = dataSize & 0xFF;
  buffer[41] = (dataSize >> 8) & 0xFF;
  buffer[42] = (dataSize >> 16) & 0xFF;
  buffer[43] = (dataSize >> 24) & 0xFF;
}

// ======================== WebSocket Events ====================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected from audio stream.");
      wsConnected = false;
      break;
    case WStype_CONNECTED:
      Serial.printf("[WS] Connected to audio stream at %s\n", payload);
      wsConnected = true;
      break;
    case WStype_TEXT:
      Serial.printf("[WS] Text: %s\n", payload);
      break;
    case WStype_PING:
    case WStype_PONG:
      break;
    default:
      break;
  }
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

// ======================== Send Cry Detection Audio ============
void sendCryDetectionAudio() {
  if (cryBufferOffset < AUDIO_CHUNK_BYTES) return;

  int totalBytes = WAV_HEADER_SIZE + AUDIO_CHUNK_BYTES;
  uint8_t* wavBuffer = (uint8_t*)malloc(totalBytes);
  if (!wavBuffer) {
    Serial.println("[Audio] Failed to allocate WAV send buffer!");
    cryBufferOffset = 0;
    return;
  }

  // Write WAV header + copy PCM data
  writeWavHeader(wavBuffer, AUDIO_CHUNK_BYTES);
  memcpy(wavBuffer + WAV_HEADER_SIZE, cryBuffer, AUDIO_CHUNK_BYTES);

  Serial.printf("[Audio] Sending %d bytes for cry detection...\n", totalBytes);

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
  cryBufferOffset = 0;
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

  // Allocate cry detection buffer
  cryBuffer = (uint8_t*)malloc(AUDIO_CHUNK_BYTES);
  if (!cryBuffer) {
    Serial.println("[Audio] FATAL: Could not allocate cry detection buffer!");
  } else {
    Serial.printf("[Audio] Cry detection buffer allocated: %d bytes\n", AUDIO_CHUNK_BYTES);
  }
  cryBufferOffset = 0;

  // Connect WiFi
  connectWiFi();

  // Initialize WebSocket for real-time audio streaming
  webSocket.begin(SERVER_HOST, SERVER_PORT, "/ws/audio-stream");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);
  Serial.println("[WS] WebSocket audio stream initialized.");

  delay(1000);
  Serial.println("[System] Setup complete. Entering main loop.\n");
}

// ======================== Loop (NON-BLOCKING) ================
void loop() {
  unsigned long now = millis();

  // ── 1. Keep WebSocket alive (MUST run every iteration) ──
  webSocket.loop();

  // ── 2. Reconnect WiFi if needed ──
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected. Reconnecting...");
    connectWiFi();
    return;
  }

  // ── 3. Read I2S audio (non-blocking, short timeout) ──
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_PORT, i2sReadBuffer,
                               I2S_READ_BYTES, &bytesRead,
                               pdMS_TO_TICKS(20));

  if (result == ESP_OK && bytesRead > 0) {
    // ── 3a. Stream to browser via WebSocket (real-time listen) ──
    if (wsConnected) {
      webSocket.sendBIN((uint8_t*)i2sReadBuffer, bytesRead);
    }

    // ── 3b. Accumulate for cry detection ──
    if (cryBuffer != NULL) {
      int remaining = AUDIO_CHUNK_BYTES - cryBufferOffset;
      int toCopy = (bytesRead < remaining) ? bytesRead : remaining;
      memcpy(cryBuffer + cryBufferOffset, i2sReadBuffer, toCopy);
      cryBufferOffset += toCopy;

      // When 3 seconds accumulated, send for cry detection
      if (cryBufferOffset >= AUDIO_CHUNK_BYTES) {
        sendCryDetectionAudio();
      }
    }
  }

  // ── 4. Send sensor data periodically ──
  if (now - lastSensorSend >= sensorInterval) {
    lastSensorSend = now;
    sendSensorData();
  }
}
