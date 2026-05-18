/*
 * AI Voice Assistant - ESP32-CAM (AI-Thinker) + INMP441
 * (Serial Monitor versiyasi - kamera ishlatilmaydi)
 *
 * Kamera moduli uzilgan, faqat ESP32 mikrokontrolleri ishlatiladi.
 * PSRAM 4MB tufayli uzun audio yozish mumkin.
 *
 * Foydalanish:
 *   1) Tashqi tugmani (GPIO12) bosib turing -> mikrofon yoziladi
 *   2) Tugmani qo'yib yuboring -> audio serverga yuboriladi
 *   3) Javob Serial Monitor'da chiqadi (115200 baud)
 *
 * Arduino IDE sozlamalari:
 *   - Board: "AI Thinker ESP32-CAM"
 *   - CPU Frequency: 240 MHz
 *   - Flash Mode: QIO
 *   - Flash Frequency: 80 MHz
 *   - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * Kerakli kutubxonalar yo'q (faqat built-in WiFi va I2S)
 *
 * Pinlar:
 *   INMP441 (I2S):
 *     VDD -> 3V3
 *     GND -> GND
 *     L/R -> GND
 *     WS  -> GPIO 14
 *     SCK -> GPIO 15
 *     SD  -> GPIO 13
 *
 *   Tugma:
 *     GPIO 12 -> GND (push button)
 *
 * Programming uchun:
 *   - GPIO 0 -> GND (yuklash paytida)
 *   - U0R (RX) -> FTDI TX
 *   - U0T (TX) -> FTDI RX
 *   - 5V -> FTDI 5V
 *   - GND -> FTDI GND
 *   - Yuklashdan oldin GPIO 0 ni GND ga ulang, RST tugmasini bosing
 *   - Yuklash tugagach, GPIO 0 ni uzing, RST ni qaytadan bosing
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// ---------------- KONFIG ----------------
#define WIFI_SSID      "MacHotspot"
#define WIFI_PASSWORD  "abcd1234"
#define SERVER_URL     "http://192.168.2.1:8000/ask"
#define LANG           "uz"   // "uz" | "en" | "auto"

// Tugma (tashqi push button)
#define BUTTON_PIN     12

// I2S (INMP441) - ESP32-CAM pinlari
#define I2S_PORT       I2S_NUM_0
#define I2S_WS         14
#define I2S_SCK        15
#define I2S_SD         13
#define SAMPLE_RATE    16000
#define MAX_REC_SEC    15      // ESP32-CAM PSRAM tufayli uzunroq

// PSRAM tufayli katta buffer (~15 sek audio)
#define MAX_AUDIO_BYTES (480 * 1024)
static uint8_t *audioBuf = nullptr;

// ---------------- I2S MIKROFON ----------------
void initMic() {
  i2s_config_t i2s_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

// 32-bit sample'ni 16-bit signed ga aylantirish
inline int16_t convertSample(int32_t raw) {
  // INMP441 24-bit ma'lumotni 32-bit MSB-aligned beradi
  int32_t s = raw >> 14;       // 18-bit signed -> kichraytirish
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

// ---------------- WAV HEADER ----------------
size_t buildWavHeader(uint8_t *buf, uint32_t dataBytes) {
  uint32_t fileSize = dataBytes + 36;
  uint32_t byteRate = SAMPLE_RATE * 2;
  memcpy(buf, "RIFF", 4);
  buf[4]  = fileSize        & 0xff;
  buf[5]  = (fileSize >> 8)  & 0xff;
  buf[6]  = (fileSize >> 16) & 0xff;
  buf[7]  = (fileSize >> 24) & 0xff;
  memcpy(buf + 8, "WAVEfmt ", 8);
  buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
  buf[20] = 1;  buf[21] = 0;
  buf[22] = 1;  buf[23] = 0;
  buf[24] = SAMPLE_RATE        & 0xff;
  buf[25] = (SAMPLE_RATE >> 8)  & 0xff;
  buf[26] = (SAMPLE_RATE >> 16) & 0xff;
  buf[27] = (SAMPLE_RATE >> 24) & 0xff;
  buf[28] = byteRate        & 0xff;
  buf[29] = (byteRate >> 8)  & 0xff;
  buf[30] = (byteRate >> 16) & 0xff;
  buf[31] = (byteRate >> 24) & 0xff;
  buf[32] = 2; buf[33] = 0;
  buf[34] = 16; buf[35] = 0;
  memcpy(buf + 36, "data", 4);
  buf[40] = dataBytes        & 0xff;
  buf[41] = (dataBytes >> 8)  & 0xff;
  buf[42] = (dataBytes >> 16) & 0xff;
  buf[43] = (dataBytes >> 24) & 0xff;
  return 44;
}

// ---------------- AUDIO YOZIB SERVERGA YUBORISH ----------------
String recordAndSend() {
  Serial.println();
  Serial.println("==============================");
  Serial.println(">>> YOZILMOQDA... GAPIRING");
  Serial.println("==============================");

  // WAV: avval 44 baytni bo'sh qoldiramiz, oxirida to'ldiramiz
  size_t written = 44;
  unsigned long startMs = millis();

  const size_t I2S_CHUNK = 256;
  int32_t i2sBuf[I2S_CHUNK];

  while (digitalRead(BUTTON_PIN) == LOW &&
         written + I2S_CHUNK * 2 <= MAX_AUDIO_BYTES &&
         (millis() - startMs) < (uint32_t)MAX_REC_SEC * 1000) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, (void*)i2sBuf, sizeof(i2sBuf), &bytesRead, portMAX_DELAY);
    int samples = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samples && written + 2 <= MAX_AUDIO_BYTES; i++) {
      int16_t s = convertSample(i2sBuf[i]);
      audioBuf[written++] = s & 0xff;
      audioBuf[written++] = (s >> 8) & 0xff;
    }
    yield();
  }

  uint32_t dataBytes = written - 44;
  uint32_t durationMs = millis() - startMs;
  buildWavHeader(audioBuf, dataBytes);

  Serial.print(">>> Yozildi: ");
  Serial.print(dataBytes);
  Serial.print(" bayt, ");
  Serial.print(durationMs);
  Serial.println(" ms");
  Serial.println(">>> Serverga yuborilmoqda...");

  HTTPClient http;
  WiFiClient client;
  http.begin(client, String(SERVER_URL) + "?lang=" + LANG);
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(30000);

  unsigned long sendStart = millis();
  int code = http.POST(audioBuf, written);
  unsigned long sendDuration = millis() - sendStart;

  String reply;
  if (code == 200) {
    reply = http.getString();
    Serial.print(">>> HTTP 200 OK (");
    Serial.print(sendDuration);
    Serial.println(" ms)");
  } else {
    reply = "HTTP xatosi: " + String(code);
    Serial.print(">>> XATO: HTTP ");
    Serial.println(code);
  }
  http.end();
  return reply;
}

// ---------------- WIFI ----------------
void connectWifi() {
  Serial.print("WiFi ulanmoqda: ");
  Serial.print(WIFI_SSID);
  Serial.print(" ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WiFi XATO! Tarmoq topilmadi yoki parol noto'g'ri.");
  }
}

// ---------------- SETUP / LOOP ----------------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("================================");
  Serial.println("AI Voice Assistant - ESP32-CAM");
  Serial.println("(Serial Monitor mode)");
  Serial.println("================================");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // PSRAM dan audio buffer (ESP32-CAM da 4MB PSRAM bor)
  if (psramFound()) {
    Serial.println("PSRAM topildi - katta buffer ishlatamiz");
    audioBuf = (uint8_t*)ps_malloc(MAX_AUDIO_BYTES);
  } else {
    Serial.println("PSRAM yo'q - oddiy RAM");
    audioBuf = (uint8_t*)malloc(MAX_AUDIO_BYTES);
  }

  if (!audioBuf) {
    Serial.println("XATO: RAM yetmadi!");
    while (true) delay(1000);
  }
  Serial.print("Audio buffer: ");
  Serial.print(MAX_AUDIO_BYTES / 1024);
  Serial.print(" KB (max ");
  Serial.print(MAX_REC_SEC);
  Serial.println(" sek)");

  connectWifi();

  Serial.println("I2S mikrofon ishga tushirilmoqda...");
  initMic();
  Serial.println("Mikrofon tayyor.");

  Serial.println();
  Serial.println("================================");
  Serial.println(">>> TAYYOR!");
  Serial.println(">>> GPIO12 tugmani bosib turing");
  Serial.println(">>> va savol bering");
  Serial.println("================================");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);
    if (digitalRead(BUTTON_PIN) == LOW) {
      String answer = recordAndSend();
      if (answer.length() == 0) answer = "(bo'sh javob)";

      Serial.println();
      Serial.println("================================");
      Serial.println(">>> JAVOB:");
      Serial.println("================================");
      Serial.println(answer);
      Serial.println("================================");
      Serial.println();
      Serial.println(">>> Yana savol uchun tugmani bosing");
    }
  }
  delay(20);
}
