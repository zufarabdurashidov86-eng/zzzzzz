/*
 * AI Voice Assistant - NodeMCU ESP8266 + INMP441 + OLED 0.96"
 *
 * Foydalanish:
 *   1) Tugmani (D3, GPIO0) bosib turing -> mikrofon yoziladi
 *   2) Tugmani qo'yib yuboring -> audio serverga yuboriladi
 *   3) Server STT + LLM ishlatib javob qaytaradi
 *   4) Javob OLED'da satrma-satr chiqadi
 *
 * Tashqi kutubxonalar (Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - ESP8266WiFi (built-in)
 *   - ESP8266HTTPClient (built-in)
 *   - I2S (built-in, ESP8266 core ichida)
 *
 * Pinlar:
 *   INMP441:  WS=D5(GPIO14), SCK=D7(GPIO13), SD=D6(GPIO12), L/R=GND
 *   OLED:     SDA=D2(GPIO4), SCL=D1(GPIO5)
 *   Button:   D3(GPIO0) -> GND
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <I2S.h>
#include <i2s_reg.h>

// ---------- KONFIG ----------
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
#define SERVER_URL     "http://192.168.1.100:8000/ask"   // server manzili
#define LANG           "uz"   // "uz" yoki "en" yoki "auto"

#define BUTTON_PIN     0      // D3
#define SAMPLE_RATE    16000
#define MAX_REC_SEC    8      // max yozish vaqti (xavfsizlik)

// OLED
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDR      0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- HOLAT ----------
WiFiClient wifiClient;

// ---------- OLED HELPERS ----------
void oledStatus(const String &msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// Matnni satrlarga bo'lib chiqarish (word-wrap)
void oledShowAnswer(const String &text) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const int charsPerLine = 21;   // 6px font @ 128px
  const int linesPerScreen = 8;  // 8 lines @ 64px

  int total = text.length();
  int pos = 0;
  int linesShown = 0;

  display.setCursor(0, 0);

  while (pos < total) {
    // bitta satrga sig'adigan miqdor
    int end = pos + charsPerLine;
    if (end > total) end = total;

    // so'z chegarasida sindirish
    if (end < total) {
      int sp = end;
      while (sp > pos && text.charAt(sp) != ' ') sp--;
      if (sp > pos) end = sp;
    }

    String line = text.substring(pos, end);
    line.trim();
    display.println(line);
    linesShown++;
    pos = end;
    while (pos < total && text.charAt(pos) == ' ') pos++;

    if (linesShown >= linesPerScreen) {
      display.display();
      delay(2500);                 // o'qishga vaqt
      display.clearDisplay();
      display.setCursor(0, 0);
      linesShown = 0;
    }
  }

  if (linesShown > 0) display.display();
}

// ---------- I2S MIKROFON ----------
void initMic() {
  // ESP8266 I2S RX rejimi (INMP441 24-bit ma'lumot, biz 16-bit qilamiz)
  i2s_rxtx_begin(true, false);    // RX yoqilgan, TX o'chirilgan
  i2s_set_rate(SAMPLE_RATE);
}

// Bir frame (16-bit sample) o'qish
// INMP441 32-bit chiqaradi, biz yuqori 16-bit ni olamiz
inline int16_t readSample() {
  uint32_t raw = 0;
  // i2s_read_sample bloklovchi funksiya
  // ESP8266 I2S kutubxonasida raw 32-bit keladi
  i2s_read_sample((uint32_t*)&raw, (uint32_t*)&raw, true);
  // yuqori 16 bit
  int32_t s = (int32_t)raw;
  s >>= 14;   // 32-bit -> ~18-bit signed -> shkaladan kichraytiramiz
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

// ---------- WAV HEADER ----------
// Streaming uchun: avval header, keyin data, content-length oldindan ma'lum bo'ladi
size_t buildWavHeader(uint8_t *buf, uint32_t dataBytes) {
  uint32_t fileSize = dataBytes + 36;
  uint32_t byteRate = SAMPLE_RATE * 2;   // mono 16-bit
  memcpy(buf, "RIFF", 4);
  buf[4] = fileSize & 0xff;
  buf[5] = (fileSize >> 8) & 0xff;
  buf[6] = (fileSize >> 16) & 0xff;
  buf[7] = (fileSize >> 24) & 0xff;
  memcpy(buf + 8, "WAVEfmt ", 8);
  buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;       // fmt chunk size
  buf[20] = 1;  buf[21] = 0;                                  // PCM
  buf[22] = 1;  buf[23] = 0;                                  // mono
  buf[24] = SAMPLE_RATE & 0xff;
  buf[25] = (SAMPLE_RATE >> 8) & 0xff;
  buf[26] = (SAMPLE_RATE >> 16) & 0xff;
  buf[27] = (SAMPLE_RATE >> 24) & 0xff;
  buf[28] = byteRate & 0xff;
  buf[29] = (byteRate >> 8) & 0xff;
  buf[30] = (byteRate >> 16) & 0xff;
  buf[31] = (byteRate >> 24) & 0xff;
  buf[32] = 2; buf[33] = 0;                                   // block align
  buf[34] = 16; buf[35] = 0;                                  // bits/sample
  memcpy(buf + 36, "data", 4);
  buf[40] = dataBytes & 0xff;
  buf[41] = (dataBytes >> 8) & 0xff;
  buf[42] = (dataBytes >> 16) & 0xff;
  buf[43] = (dataBytes >> 24) & 0xff;
  return 44;
}

// ---------- AUDIO YOZIB SERVERGA YUBORISH ----------
String recordAndSend() {
  oledStatus("Yozilmoqda...\nGapiring");

  // Tugma bosilgan vaqt davomida sample yig'amiz, 16 KB chunk'larda RAM ga
  // ESP8266 da heap chegarasi bor. ~64 KB gacha xavfsiz.
  const size_t MAX_BYTES = 64 * 1024;  // ~2 sek @ 16kHz/16bit (mono = 32 KB/sek) -> 2 sek
  // Eslatma: agar ko'proq kerak bo'lsa, chunked POST va parallel oqim qiling.
  uint8_t *buffer = (uint8_t*)malloc(MAX_BYTES);
  if (!buffer) {
    oledStatus("RAM yetmadi!");
    return "";
  }

  size_t written = 0;
  unsigned long startMs = millis();
  while (digitalRead(BUTTON_PIN) == LOW &&
         written + 2 <= MAX_BYTES &&
         (millis() - startMs) < (uint32_t)MAX_REC_SEC * 1000) {
    int16_t s = readSample();
    buffer[written++] = s & 0xff;
    buffer[written++] = (s >> 8) & 0xff;
    yield();
  }

  oledStatus("Yuborilmoqda...");

  // WAV header tayyorlash
  uint8_t header[44];
  buildWavHeader(header, written);

  HTTPClient http;
  http.begin(wifiClient, String(SERVER_URL) + "?lang=" + LANG);
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(30000);

  // Header + data ni bitta blokga birlashtirish
  // (kichik audio uchun amaliy)
  size_t totalLen = 44 + written;
  uint8_t *payload = (uint8_t*)malloc(totalLen);
  if (!payload) {
    free(buffer);
    oledStatus("RAM yetmadi!");
    return "";
  }
  memcpy(payload, header, 44);
  memcpy(payload + 44, buffer, written);
  free(buffer);

  int code = http.POST(payload, totalLen);
  free(payload);

  String reply = "";
  if (code == 200) {
    reply = http.getString();
  } else {
    reply = "Xatolik: HTTP " + String(code);
  }
  http.end();
  return reply;
}

// ---------- WIFI ----------
void connectWifi() {
  oledStatus("WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    oledStatus("WiFi OK\n" + WiFi.localIP().toString());
    delay(1200);
  } else {
    oledStatus("WiFi xato!");
  }
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();          // SDA=D2, SCL=D1
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 topilmadi");
  }
  display.clearDisplay();
  display.display();

  oledStatus("AI Yordamchi\nIshga tushyapti");
  delay(800);

  connectWifi();
  initMic();

  oledStatus("Tayyor!\nTugmani bosib\ngapiring");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);  // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      String answer = recordAndSend();
      if (answer.length() == 0) answer = "(bo'sh javob)";
      oledShowAnswer(answer);
      delay(1500);
      oledStatus("Tayyor!\nTugmani bosib\ngapiring");
    }
  }
  delay(20);
}
