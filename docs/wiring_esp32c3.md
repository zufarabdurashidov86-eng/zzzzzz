# ESP32-C3 SuperMini ulashlari

## TFT 1.8" ST7735 (SPI)

| TFT pin | ESP32-C3 GPIO | Izoh |
|---------|---------------|------|
| VCC     | 3V3           | quvvat |
| GND     | GND           | yer |
| SCL/SCK | GPIO4         | SPI clock |
| SDA/MOSI| GPIO6         | SPI data |
| RES     | GPIO5         | reset |
| DC/RS   | GPIO10        | data/command |
| CS      | GPIO7         | chip select |
| BLK     | 3V3           | doimo yoritilgan (yoki PWM uchun GPIO8) |

## INMP441 (I2S)

| INMP441 | ESP32-C3 GPIO |
|---------|---------------|
| VDD     | 3V3           |
| GND     | GND           |
| L/R     | GND (left ch) |
| WS      | GPIO1         |
| SCK     | GPIO0         |
| SD      | GPIO3         |

## Tugma

`GPIO9` (BOOT tugmasi) — qo'shimcha sim shart emas. Yoki tashqi tugma:
`GPIO9` ↔ `GND`.

## Diagramma (ASCII)

```
                 ESP32-C3 SuperMini
                +-----------------+
                |  USB-C          |
                |                 |
   TFT SCK <----|GPIO4       3V3 |---> TFT VCC, INMP441 VDD
   TFT RES <----|GPIO5       GND |---> TFT GND, INMP441 GND, L/R
   TFT MOSI<----|GPIO6            |
   TFT CS  <----|GPIO7            |
                |GPIO8            |
                |GPIO9 (BOOT btn) |
   TFT DC  <----|GPIO10           |
                |                 |
   I2S SCK <----|GPIO0            |
   I2S WS  <----|GPIO1            |
   I2S SD  <----|GPIO3            |
                +-----------------+
```

## Joriy ishlatilmagan GPIO

`GPIO2`, `GPIO8`, `GPIO20`, `GPIO21` — kelajakda LED, ikkinchi tugma yoki
PWM backlight uchun ishlatilishi mumkin.

> **Ogohlantirish:** GPIO8 va GPIO9 ni boshqa maqsadga ishlatishdan oldin
> ESP32-C3 strapping pin cheklovlarini tekshiring (boot rejimiga ta'sir qiladi).
