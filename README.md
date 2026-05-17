# ESP8266 AI Voice Assistant (uz / en)

NodeMCU ESP8266 + INMP441 mikrofon + 0.96" OLED yordamida ovozli AI yordamchi.
Foydalanuvchi tugmani bosib mikrofonga gapiradi (o'zbek yoki ingliz tilida),
javob OLED ekranida chiqadi.

## Qanday ishlaydi

```
[INMP441] --I2S--> [ESP8266] --HTTP POST WAV--> [Python server (FastAPI)]
                                                       |
                                                       v
                                          Whisper (STT, uz/en autodetect)
                                                       |
                                                       v
                                                OpenAI GPT-4o-mini
                                                       |
[OLED 0.96"] <-- HTTP javob (matn) -- [ESP8266] <-----+
```

## Qurilmalar va sim ulashlari

### INMP441 -> NodeMCU ESP8266

| INMP441 | ESP8266 (NodeMCU pin) |
|---------|-----------------------|
| VDD     | 3V3                   |
| GND     | GND                   |
| L/R     | GND (left channel)    |
| WS      | D5  (GPIO14)          |
| SCK     | D7  (GPIO13)          |
| SD      | D6  (GPIO12)          |

### OLED SSD1306 0.96" (I2C) -> NodeMCU

| OLED | ESP8266 |
|------|---------|
| VCC  | 3V3     |
| GND  | GND     |
| SDA  | D2 (GPIO4) |
| SCL  | D1 (GPIO5) |

### Tugma

`D3 (GPIO0)` -> `GND` (ichki pull-up yoqilgan).

## Server (Python)

### Talablar
- Python 3.10+
- OpenAI API kaliti

### O'rnatish

```bash
cd server
python -m venv .venv
source .venv/bin/activate     # Windows: .venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env
# .env fayliga OPENAI_API_KEY ni qo'ying
```

### Ishga tushirish

```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```

Server `0.0.0.0:8000` da tinglaydi. ESP8266 va kompyuter bitta WiFi tarmog'ida
bo'lishi kerak.

### Sinov

```bash
# istalgan WAV fayl bilan
curl -X POST "http://localhost:8000/ask?lang=uz" \
     -H "Content-Type: audio/wav" \
     --data-binary @sample.wav
```

## Firmware (Arduino IDE)

### Talab qilingan kutubxonalar (Library Manager orqali)
- **Adafruit GFX Library**
- **Adafruit SSD1306**

`I2S` kutubxonasi ESP8266 core ichida (alohida o'rnatish shart emas).

### Sozlash

`firmware/firmware.ino` fayli boshida quyidagilarni o'zgartiring:

```cpp
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
#define SERVER_URL     "http://192.168.1.100:8000/ask"
#define LANG           "uz"   // "uz" | "en" | "auto"
```

`SERVER_URL` da `192.168.1.100` ni serveringiz lokal IP manziliga almashtiring
(Windows: `ipconfig`, Linux/Mac: `ip a`).

### Yuklash
1. Arduino IDE -> Board: **NodeMCU 1.0 (ESP-12E Module)**
2. Tools -> CPU Frequency: **160 MHz** (audio uchun)
3. Tools -> Flash Size: **4MB (FS:2MB OTA:~1019KB)**
4. Upload tugmasini bosing.

## Foydalanish

1. NodeMCU quvvatlansin -> OLED'da `WiFi OK` -> `Tayyor!` ko'rinadi.
2. Tugmani **bosib turing** va savol bering (max 8 soniya).
3. Tugmani qo'yib yuboring -> `Yuborilmoqda...` -> bir necha soniyadan keyin
   javob OLED'da chiqadi.
4. Uzun javob bo'lsa avtomatik bo'lib-bo'lib ko'rsatiladi.

## ESP8266 cheklovlari

- ESP8266 da RAM ~80 KB. Hozirgi konfiguratsiya audio'ni RAM ga yig'adi
  (~64 KB = ~2 sekund). Uzunroq yozuv kerak bo'lsa:
  - **chunked HTTP streaming** ga o'ting (audio'ni qism-qism yuborish), yoki
  - **ESP32** ga o'ting (~520 KB RAM, PSRAM bilan ko'proq).
- ESP32 ga o'tganingizda firmware ning faqat `I2S` qismi o'zgaradi
  (`driver/i2s.h` API), qolgan kod deyarli mos keladi.

## Mumkin bo'lgan kengaytmalar

- TTS (matnni audio'ga aylantirish) qo'shib OLED + dinamik javob
- Suhbat tarixi (kontekstli savollar)
- Wake-word ("Salom Olim") - tugmasiz ishlash
- Lokal Whisper model o'rniga `openai/whisper-1` API (server resursini tejash)
- Telegram bot orqali javoblarni dublikatlash
