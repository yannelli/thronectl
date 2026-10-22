# LED output

Created: 2026-10-22. Last updated: 2026-10-22.

Read this before changing `firmware/src/hal/led.cpp`, `hal/system.cpp` or `core/app.cpp`.

The Stamp-S3A LED is a WS2812B-2020 on GPIO 21. Its supply shares the display backlight switch on GPIO 38. M5GFX 0.2.34 drives this switch with 256 Hz PWM below full brightness. An active LED effect now holds the backlight at 255, including its dark phases and preview gaps. Disabling the effect restores the last requested display brightness. A sleeping screen follows the existing "With screen off" setting. This policy costs battery and overrides display dimming while an effect is active. The board wiring does not provide independent control of these two supplies. [M5Stack Stamp-S3A documentation](https://docs.m5stack.com/en/core/Stamp-S3A)

Arduino-ESP32 2.0.17 `neopixelWrite` already uses blocking RMT output. It does not bit-bang GPIO under software interrupt timing. Its zero-bit high pulse is 400 ns. The WS2812B-2020 V1.0 datasheet specifies 220 to 380 ns for that pulse. The v0.6.0 writer uses these timings:

| Part of frame | Duration |
|---|---|
| Zero high / low | 300 / 950 ns |
| One high / low | 900 / 350 ns |
| Reset before and after the data | 300 us low each |

The color order is green, red, blue, with the most significant bit first. The reset exceeds the datasheet's 280 us minimum. [Worldsemi WS2812B-2020 V1.0 datasheet, page 3](https://www.mouser.com/pdfDocs/WS2812B-2020_V10_EN_181106150240761.pdf)

One RMT memory block holds both resets and all 24 bits. The peripheral generates the edges without an interrupt refill, so Wi-Fi and Bluetooth scheduling can delay a frame without changing its pulse lengths. The call blocks until transmission ends. The app updates effects at most once per 16 ms; unchanged colors are skipped. Panel sleep or wake invalidates the saved color so it is sent again after power returns. [ESP-IDF 4.4.7 RMT documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/rmt.html)

`bash firmware/tools/ledcheck/run.sh` tests the actual output code with fake display and RMT calls under ASan and UBSan. It checks power through dark phases, brightness restoration, sleep/wake invalidation, retry after failure, color order and pulse limits. Keyfuzz also checks that the app keeps power on through blink and preview gaps.

Real-device LED behavior and Wi-Fi/Bluetooth traffic still need hardware verification. The host tests check the requested waveform and power policy, without measuring the physical signal or supply.
