/*
 * TFT 1.8" ST7735 - kengaytirilgan diagnostik test
 *
 * Tekshiradi:
 *  - Manual reset (RES pinini qo'l bilan tortish)
 *  - 4 xil initR variantini ketma-ket sinaydi
 *  - Har bir variant uchun rang flesh + matn ko'rsatadi
 *  - Serial'da har qadamni log qiladi
 *
 * Pinlar:
 *   SCK=GPIO4, MOSI=GPIO6, RES=GPIO5, DC=GPIO10, CS=GPIO7
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define TFT_CS    7
#define TFT_DC    10
#define TFT_RST   5
#define TFT_MOSI  6
#define TFT_SCLK  4

SPIClass tftSPI(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

// Manual reset - ba'zi modullar bunga ehtiyoj sezadi
void hardReset() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(50);
  digitalWrite(TFT_RST, HIGH); delay(150);
}

void runTest(const char *label, uint8_t variant) {
  Serial.print(">>> Test: ");
  Serial.println(label);

  hardReset();
  tft.initR(variant);
  tft.setRotation(0);

  // 1. Qora -> sim ulanganmi tekshirish
  tft.fillScreen(ST77XX_BLACK);
  delay(400);

  // 2. Rang flesh - SPI ma'lumot oqyaptimi tekshirish
  tft.fillScreen(ST77XX_RED);   delay(500);
  tft.fillScreen(ST77XX_GREEN); delay(500);
  tft.fillScreen(ST77XX_BLUE);  delay(500);

  // 3. Matn
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 20);
  tft.println(label);
  tft.setTextSize(1);
  tft.setCursor(5, 60);
  tft.println("Agar bu matn");
  tft.setCursor(5, 75);
  tft.println("ko'rinsa - to'g'ri");
  tft.setCursor(5, 95);
  tft.println("variant TOPILDI!");

  delay(3000);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=== TFT KENGAYTIRILGAN TEST ===");
  Serial.println("Pinlar: SCK=4, MOSI=6, RST=5, DC=10, CS=7");

  // SPI ni boshlash
  tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  Serial.println("SPI tayyor");

  // 4 xil variant ketma-ket
  runTest("BLACKTAB",    INITR_BLACKTAB);
  runTest("GREENTAB",    INITR_GREENTAB);
  runTest("REDTAB",      INITR_REDTAB);
  runTest("144GREENTAB", INITR_144GREENTAB);

  Serial.println("=== Test sikli tugadi, qaytaramiz ===");
}

void loop() {
  // Bir necha sek kutib qayta sinaymiz
  delay(2000);
  setup();
}
