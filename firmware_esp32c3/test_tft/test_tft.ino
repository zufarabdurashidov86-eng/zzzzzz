/*
 * TFT 1.8" ST7735 - faqat ekran testi
 * Hech qanday WiFi, mikrofon va boshqa narsa yo'q.
 * Maqsad: ekran sim ulashlari to'g'ri ishlayotganini tekshirish.
 *
 * Pinlar (firmware bilan bir xil):
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

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=== TFT TEST BOSHLANDI ===");
  Serial.println("Pinlar: SCK=4, MOSI=6, RST=5, DC=10, CS=7");

  tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  Serial.println("SPI ishga tushirildi");

  // 4 xil initR variantini ketma-ket sinab ko'ramiz
  // Eng keng tarqalganidan boshlaymiz
  tft.initR(INITR_BLACKTAB);
  Serial.println("initR(BLACKTAB) chaqirildi");

  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  delay(500);

  Serial.println("Ekran qora qilindi - ko'ryapsizmi?");

  // Test rangli kvadratlar
  tft.fillScreen(ST77XX_RED);    delay(700);
  tft.fillScreen(ST77XX_GREEN);  delay(700);
  tft.fillScreen(ST77XX_BLUE);   delay(700);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("TFT OK!");
  tft.setTextSize(1);
  tft.setCursor(10, 60);
  tft.println("Sim to'g'ri.");
  tft.setCursor(10, 75);
  tft.println("BLACKTAB variant.");

  Serial.println("Test tugadi. Ekranda 'TFT OK!' ko'rinishi kerak.");
}

void loop() {
  delay(1000);
}
