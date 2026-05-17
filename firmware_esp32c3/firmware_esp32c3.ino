/*
 * AI Voice Assistant - ESP32-C3 SuperMini + INMP441 + TFT 1.8" ST7735
 *
 * Foydalanish:
 *   1) BOOT tugmani (GPIO9) bosib turing -> mikrofon yoziladi
 *   2) Tugmani qo'yib yuboring -> audio serverga yuboriladi
 *   3) TFT ekranda javob ko'rinadi
 *
 * Arduino IDE sozlamalari:
 *   - Boards Manager: "esp32" by Espressif >= 3.0
 *   - Board: "ESP32C3 Dev Module"
 *   - USB CDC On Boot: "Enabled"
 *   - CPU Frequency: 160 MHz
 *   - Flash Size: 4MB
 *   - Partition Scheme: "Default 4MB with spiffs"
 *
 * Kutubxonalar (Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *
 * Pinlar:
 *   TFT 1.8" ST7735 (SPI):
 *     SCK=GPIO4, MOSI=GPIO6, RES=GPIO5, DC=GPIO10, CS=GPIO7
 *   INMP441 (I2S):
 *     WS=GPIO1, SCK=GPIO0, SD=GPIO3, L/R=GND
 *   Button: GPIO9 (BOOT tugmasi yoki tashqi tugma -> GND)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <driver/i2s.h>

// ---------------- KONFIG ----------------
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
#define SERVER_URL     "http://192.168.1.100:8000/ask"
#define LANG           "uz"   // "uz" | "en" | "auto"

// Tugma
#define BUTTON_PIN     9      // BOOT tugmasi

// I2S (INMP441)
#define I2S_PORT       I2S_NUM_0
#define I2S_WS         1
#define I2S_SCK        0
#define I2S_SD         3
#define SAMPLE_RATE    16000
#define MAX_REC_SEC    8

// TFT (ST7735, 128x160)
#define TFT_CS    7
#define TFT_DC    10
#define TFT_RST   5
#define TFT_MOSI  6
#define TFT_SCLK  4
// SPI hardware: ESP32-C3 da custom pinlar SPIClass orqali

SPIClass tftSPI(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

// Buffer (ESP32-C3 ~320KB RAM, 256KB ni xavfsiz olamiz -> ~8 sek)
#define MAX_AUDIO_BYTES (256 * 1024)
static uint8_t *audioBuf = nullptr;

// ---------------- TFT HELPERS ----------------
void tftStatus(const String &line1, const String &line2 = "") {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(4, 10);
  tft.println(line1);
  if (line2.length()) {
    tft.setTextSize(1);
    tft.setCursor(4, 50);
    tft.println(line2);
  }
}

// Word-wrap matn chiqarish, kerak bo'lsa sahifalab
void tftShowAnswer(const String &text) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);

  // 128 px width / 6 px per char ~ 21 char/line
  // 160 px height / 10 px per line ~ 16 lines
  const int charsPerLine = 21;
  const int linesPerScreen = 15;

  int total = text.length();
  int pos = 0;
  int linesShown = 0;
  tft.setCursor(2, 2);

  while (pos < total) {
    int end = pos + charsPerLine;
    if (end > total) end = total;

    if (end < total) {
      int sp = end;
      while (sp > pos && text.charAt(sp) != ' ') sp--;
      if (sp > pos) end = sp;
    }

    String line = text.substring(pos, end);
    line.trim();
    tft.println(line);
    linesShown++;
    pos = end;
    while (pos < total && text.charAt(pos) == ' ') pos++;

    if (linesShown >= linesPerScreen && pos < total) {
      delay(3000);
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(2, 2);
      linesShown = 0;
    }
  }
}

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
  tftStatus("Yozilmoqda", "Gapiring...");

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
  buildWavHeader(audioBuf, dataBytes);

  tftStatus("Yuborilmoqda", String(dataBytes / 1024) + " KB");

  HTTPClient http;
  WiFiClient client;
  http.begin(client, String(SERVER_URL) + "?lang=" + LANG);
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(30000);

  int code = http.POST(audioBuf, written);
  String reply;
  if (code == 200) {
    reply = http.getString();
  } else {
    reply = "HTTP xatosi: " + String(code);
  }
  http.end();
  return reply;
}

// ---------------- WIFI ----------------
void connectWifi() {
  tftStatus("WiFi", "ulanmoqda...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    tftStatus("WiFi OK", WiFi.localIP().toString());
    delay(1200);
  } else {
    tftStatus("WiFi", "XATO!");
    delay(2000);
  }
}

// ---------------- SETUP / LOOP ----------------
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // SPI custom pinlar bilan
  tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);     // 1.8" 128x160 ko'p variantlar uchun
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  tftStatus("AI Yordamchi", "ishga tushyapti...");

  // RAM dan audio buffer
  audioBuf = (uint8_t*)malloc(MAX_AUDIO_BYTES);
  if (!audioBuf) {
    tftStatus("XATO", "RAM yetmadi");
    while (true) delay(1000);
  }

  connectWifi();
  initMic();

  tftStatus("Tayyor!", "Tugmani bosib gapiring");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);
    if (digitalRead(BUTTON_PIN) == LOW) {
      String answer = recordAndSend();
      if (answer.length() == 0) answer = "(bo'sh javob)";
      tftShowAnswer(answer);
      delay(2000);
      tftStatus("Tayyor!", "Tugmani bosib gapiring");
    }
  }
  delay(20);
}
