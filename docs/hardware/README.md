# Cardputer-Adv hardware reference

Board: M5Stack Cardputer-Adv (K132-Adv) with the Stamp-S3A module (ESP32-S3FN8, 8 MB flash, no PSRAM).
Toolchain: PlatformIO espressif32@6.10.0, Arduino-ESP32 2.0.17 on ESP-IDF 4.4.7.
Libraries: M5Unified 0.2.27, M5Cardputer 1.1.1, M5GFX 0.2.34 (in `firmware/.pio/libdeps/cardputer-adv`).
Sources are the M5Stack product pages, the schematics `Sch_M5CardputerAdv_v1.0` and `Sch_StampS3_v0.3.3`, and the library code (see "Sources").
Library paths below are relative to `firmware/.pio/libdeps/cardputer-adv/`.

## Pin map

| GPIO | Signal | Part | Notes |
|------|--------|------|-------|
| G0 | BOOT button (BTN2) | ESP32-S3 strap | 10k pull-up. RTC GPIO. `M5.BtnA`. Low at reset enters download mode. |
| EN | RESET button (BTN1) | CHIP_PU | 10k pull-up. |
| G1 | Grove SCL (white) | J2 HY2.0-4P | 10k pull-up. `M5.Ex_I2C` (I2C_NUM_0). |
| G2 | Grove SDA (yellow) | J2 HY2.0-4P | 10k pull-up. |
| G3 | EXT RESET | 2x7 header | Strap pin (JTAG source select). |
| G4 | EXT INT | 2x7 header | |
| G5 | EXT CS | 2x7 header | CS for a Cap module (LoRa NSS in M5 demo). |
| G6 | EXT BUSY | 2x7 header | |
| G7 | IO1 | internal FPC1 | Reserved internal FPC (also has G8, G9, 3V3, 5V). |
| G8 | I2C SDA | TCA8418, ES8311, BMI270, header, FPC1 | 3.3k pull-up. `M5.In_I2C` (I2C_NUM_1). |
| G9 | I2C SCL | same | 3.3k pull-up. |
| G10 | VBAT_IN / 2 | R8/R12 100k/100k, C10 100 nF | ADC1 channel 9. |
| G11 | KEY INT | TCA8418 INT | 3.3k pull-up, open drain, active low. RTC GPIO. |
| G12 | SD CS | microSD (TF-015) | 33 ohm series resistor. |
| G13 | EXT UART TX (to GPS RX) | 2x7 header | |
| G14 | SD MOSI | microSD and header | Shared with the header. |
| G15 | EXT UART RX (from GPS TX) | 2x7 header | |
| G19, G20 | USB D-, D+ | USB-C on Stamp-S3A | USB Serial/JTAG (`ARDUINO_USB_MODE=1`). |
| G21 | SK_DIN | WS2812B-2020 on Stamp-S3A | VDD is BL_3V3, not 3V3 (see RGB LED). |
| G33 | DISP_RST | ST7789V2 | |
| G34 | DISP_RS (D/C) | ST7789V2 | |
| G35 | DISP_MOSI | ST7789V2 | 3-wire SPI, also read line. |
| G36 | DISP_SCK | ST7789V2 | |
| G37 | DISP_CS | ST7789V2 | |
| G38 | DISP_BL | AW35122 load switch EN | Output BL_3V3 feeds the LCD backlight and the RGB LED. |
| G39 | SD MISO | microSD and header | Shared with the header. |
| G40 | SD SCK | microSD and header | Shared with the header. |
| G41 | I2S BCLK | ES8311 SCLK | |
| G42 | I2S data out | ES8311 DSDIN | Speaker path. |
| G43 | I2S LRCK | ES8311 LRCK | Also U0TXD (default UART0 TX, ROM boot log). |
| G44 | IR LED anode | IR1, 22 ohm to GND | Also U0RXD (default UART0 RX). TX only. |
| G46 | I2S data in | ES8311 ASDOUT | Strap pin. Must not be high at reset. |

I2C devices on G8/G9: ES8311 at 0x18 (CE tied to AGND), TCA8418 at 0x34, BMI270 at 0x69 (SDO pulled up).

2x7 header (M5 numbering, odd pins left): 1 G3 RESET, 2 5VIN, 3 G4 INT, 4 GND, 5 G6 BUSY, 6 5VOUT, 7 G40 SCK,
8 G8 SDA, 9 G14 MOSI, 10 G9 SCL, 11 G39 MISO, 12 G13 TX, 13 G5 CS, 14 G15 RX.

## Power and battery

Power path (schematic page 1):
- J1 battery goes through Q2 (reverse protection) and Q3. The side switch SW1 drives the Q3 gate. OFF disconnects the cell (0.23 uA standby per M5).
- TP4057 (U1) charges VBAT_IN. VBAT_IN is after Q3, so the cell charges only with SW1 ON. M5 says: "switch the power to ON" when charging.
- R_PROG is 3.3k. With I = 1000 * 1 V / R_PROG (datasheet: 2k = 500 mA, 10k = 100 mA) the charge current is about 300 mA.
- TP4057 CHRG (pin 1) and STDBY (pin 5) are unconnected stubs. No charge LED is fitted. D3 on VBAT_OUT is not populated.
- CN809J (U3) on 5VIN drives P-MOSFET Q1. With USB present Q1 is off and the system runs from USB through D1 (SS34). The cell only charges.
- SY7088 boost makes +5VOUT (R4 75k / R6 22k, about 5.3 V before D2). EN is tied to VIN, so it always runs.
- SY8089 buck makes +3.3V (R11 68k / R13 15k). EN is tied to VIN, always on.
- SW2 selects the Grove 5V source: +5VOUT (boost) or +5VIN (USB rail).
- Capacity: 1750 mAh on early units. M5 changed it to 1500 mAh on 2026.7.13.

Battery sense in M5Unified:
- `M5Unified/src/utility/Power_Class.inl:583-591`: board_M5CardputerADV uses ADC1 GPIO10, `pmic_adc`, `_adc_ratio = 2.0`. This matches R8/R12 (100k/100k).
- `Power_Class.inl:2112-2209`: on IDF 4.4.7 the legacy `esp_adc_cal` path runs. One sample, 12 dB (11 dB) attenuation, 12 bit, eFuse calibration.
- `Power_Class.inl:2447-2448`: `getBatteryVoltage()` returns mV = raw mV * 2.
- `Power_Class.inl:2540-2568`: `getBatteryLevel()` = (mV - 3300) * 100 / 800, clamped. Linear: 0 % at 3.30 V, 100 % at 4.10 V.
- `Power_Class.inl:2873` then `:3011-3012`: `isCharging()` returns `charge_unknown` for the ADV. Hardware cannot report charge state.
- While USB charges the cell, G10 reads the cell under charge current. CV phase reads about 4.2 V, so the level shows 100 % early.
- With SW1 OFF and USB in, G10 reads the TP4057 output with no cell. Expect values near 4.2 V or unstable values.

## RGB LED (Stamp-S3A)

- Part: WS2812B-2020, one LED, data on G21 (Stamp-S3A schematic U3, net SK_DIN). Color order GRB (M5 FastLED example: `WS2812, 21, GRB`).
- Power: VDD is BL_3V3, the output of the AW35122 load switch. G38 (DISP_BL) is its EN. BL_3V3 also feeds the LCD backlight.
- M5 docs: "set GPIO38, which controls this switch, to a high level" and "Shares power with display backlight".
- On the ADV, M5GFX owns G38 as the backlight PWM (LEDC channel 7, 256 Hz, 9 bit). Do not drive G38 with `digitalWrite`.
- Result: the LED has no power at brightness 0. Below 255 the LED supply switches on and off at 256 Hz.
  A WS2812 starts dark after a power loss, so expect the color to drop out or flicker at low brightness. Test this on a unit.
  At 255 the duty is 512/512 (steady high). 254 gives about 99.6 %, which C17 (10 uF) should bridge.
- M5Unified maps the LED to G21 (`M5Unified.inl:259`) with `pin_power = -1` (`M5Unified.inl:2254-2316`).
  On IDF 4.4 `M5UNIFIED_RMT_VERSION` is 1 (`LED_Strip_Class.hpp:17-23`), and the v1 branches of `LedBus_RMT::init/write` are empty
  (`LED_Strip_Class.inl:121` and `:179`). `init()` returns false, so `M5.Led` does nothing.
- Arduino 2.0.17 `neopixelWrite(pin, r, g, b)` (`framework-arduinoespressif32/cores/esp32/esp32-hal-rgb-led.c:4-47`) works on G21.
  It sends GRB, 24 bits, T0H 0.4 us / T1H 0.8 us, through `rmtInit(pin, RMT_TX_MODE, RMT_MEM_64)`.
  It blocks for about 30 us. It binds the RMT channel to the first pin used (static state), so call it only with G21.

## microSD

- Pins: SCK G40, MISO G39, MOSI G14, CS G12, each with a 33 ohm series resistor. No card-detect pin. Card VDD is +3.3V, always on.
- M5Unified SD table: `M5Unified.inl:222` = clk 40, mosi 14, miso 39, cs 12. M5Unified does not mount the card.
- The SD clock and data lines (G40/G14/G39) also go to the 2x7 header. A Cap module (for example Cap LoRa) shares this bus with CS on G5.
- Clock: these pins are not SPI IO_MUX pins, so signals go through the GPIO matrix.
  ESP-IDF 4.4.7: GPIO matrix full-duplex transfers "only support speeds up to 26MHz".
  The M5Cardputer Arduino example uses 25 MHz (`M5Cardputer/examples/Basic/sdcard/sdcard.ino:54`).
  M5's IDF demo uses `SDSPI_HOST_DEFAULT()` (20 MHz) and shares the bus with LoRa.
  Treat 25 MHz as the ceiling and 20 MHz as the safe value with a module on the header.
- Arduino 2.0.17 `SPI` is FSPI (SPI2). M5GFX uses SPI3 for the LCD, so they do not collide.

## Keyboard (TCA8418)

- U9 TCA8418RTWR at 0x34 on G8/G9, INT on G11. RESET is pulled up by R38 10k (no GPIO). ROW0-6 and COL0-7 are used (56 keys).
- Reader choice: `M5Cardputer/src/utility/Keyboard/Keyboard.cpp:23-24` picks `TCA8418KeyboardReader` for board_M5CardputerADV.
- `TCA8418.cpp:13,39,45`: INT pin 11, `matrix(7, 8)`, `attachInterruptArg(..., CHANGE)`. The I2C clock is 400 kHz (`Adafruit_TCA8418.h:74`).
- `TCA8418.cpp:52-73`: interrupt driven. When the ISR flag is set, `update()` reads one FIFO event, then clears INT_STAT.
  `M5Cardputer.cpp:34` calls `updateKeyList()` once per `M5Cardputer.update()`, so one event drains per main loop pass.
  The TCA8418 FIFO holds 10 events. A slow loop can overflow it and lose release events (stuck keys).
- Raw event code e = 1 + 10 * ROW + COL. `TCA8418.cpp:88-101` remaps to the Cardputer grid: x = 2 * ROW + (COL >= 4), y = COL % 4.
  Inverse: ROW = x / 2, COL = y + 4 * (x % 2).
- Legend map (`Keyboard.h:18-73`, value_first; value_second is the shifted symbol):

| y \ x | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `` ` `` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | - | = | BKSP |
| 1 | TAB | q | w | e | r | t | y | u | i | o | p | [ | ] | \ |
| 2 | FN | SHIFT | a | s | d | f | g | h | j | k | l | ; | ' | ENTER |
| 3 | CTRL | OPT | ALT | z | x | c | v | b | n | m | , | . | / | SPACE |

## Audio (ES8311 codec, NS4150B amp)

- ES8311 (U6): CCLK G9, CDATA G8, address 0x18. MCLK pin is not connected. SCLK G41, LRCK G43, DSDIN G42, ASDOUT G46.
- Output: OUTP through C30 (22 uF) is net DAC_P. It feeds the NS4150B (INP) and the 3.5 mm jack J7 (PJ-342) through R25 10 ohm. Mono.
- Mic: analog MEMS MSM381A3729H9BPC, differential into MIC1P/MIC1N.
- NS4150B CTRL is net AMP_EN, pulled up by R23 10k. The amp is on unless headphones are in.
- HP_DET: the jack switch pin 7 goes through R40 and D3 to the gate of Q4 (2N7002). With a plug in, Q4 pulls AMP_EN low.
  HP_DET and AMP_EN do not go to any GPIO. Firmware cannot read headphone state or switch the amp.
- Speaker setup (`M5Unified.inl:3104-3116`): BCK G41, WS G43, DOUT G42, I2S_NUM_1, magnification 16, enable callback for the ADV.
  Sample rate stays at the class default 48 kHz (`Speaker_Class.hpp:48`). I2S is 16 bit, mono right slot (`Speaker_Class.inl:270-274`).
- Speaker callback `_speaker_enabled_cb_cardputer_adv` (`M5Unified.inl:1045`): writes 00=80, 01=B5 (MCLK from BCLK), 02=18 (x8),
  0D=01, 12=00 (DAC on), 13=10 (HP drive on), 32=BF (0 dB), 37=08. The disable list is empty, so `Speaker.end()` leaves the DAC powered.
- MCLK = BCLK x 8 = 256 fs with 32 fs BCLK, so any rate the I2S driver makes works. Idle speaker keeps I2S running and writes zeros.
- Mic setup (`M5Unified.inl:2615-2627`, `:2758-2766`): I2S_NUM_0, DIN G46, WS G43, BCK G41, 16 kHz default, over_sampling 1.
- Mic callback `_microphone_enabled_cb_cardputer_adv` (`M5Unified.inl:1537`): 00=80, 01=BA, 02=18, 0D=01, 0E=02,
  14=10 (MIC1 input, PGA 0 dB), 17=BF (ADC 0 dB), 1C=6A. Disable: 0D=FC, 0E=6A, 00=00.
- Speaker and mic share BCK and LRCK, so only one runs at a time. M5Unified uses PGA +21 dB (14=17) for StopWatch (`M5Unified.inl:1294`).

## Display (ST7789V2)

- Autodetect: `M5GFX/src/M5GFX.cpp:2692-2795`. It reads panel ID 0x81/0x85, then G8/G9 with pull-downs. Both high (external pull-ups) means ADV.
- Bus: SPI3_HOST, 3-wire, mode 0, MOSI G35, SCLK G36, DC G34, CS G37, RST G33. Write 40 MHz, read 16 MHz (`M5GFX.cpp:2755-2757`).
  40 MHz is the GPIO matrix limit for these pins.
- Panel: 135 x 240, offset_x 52, offset_y 40, invert on, readable, default rotation 1 (`M5GFX.cpp:2762-2793`).
- Backlight: `_set_pwm_backlight(GPIO_NUM_38, 7, 256, false, 16)` (`M5GFX.cpp:2794`). 9 bit LEDC (`Light_PWM.inl:68`).
  Offset 16 sets the minimum duty for brightness 1. `sleep()` sets brightness 0 and sends SLPIN. `wakeup()` reverses it.

## IR

- IR1 is an LED from G44 to GND through R14 22 ohm. There is no receiver, so it is TX only. M5 lists "1x IR emitter".
- G44 is also U0RXD. If left as an input with a pull-up, a small current can flow through the LED.
- Libraries: IRremote 4.7.1 is already in libdeps (an M5Cardputer dependency). The M5 example `ir_nec.ino:20-22` uses
  `SEND_PWM_BY_TIMER` and `setSendPin(44)`. On core 2.x it takes LEDC channel 0 and hardware timer 1 (`IRremote/src/private/IRTimer.hpp`).
  That does not clash with the backlight on LEDC channel 7. M5's IDF demo uses RMT with a 38 kHz, 33 % carrier (`ir_helper.c`).
  On core 2.0.17 the matching path is `rmtInit` plus `rmtSetCarrier` from `esp32-hal-rmt`, which shares the RMT allocator with `neopixelWrite`.

## IMU (BMI270)

- U7 at 0x69 on G8/G9. INT1 and INT2 are not connected (marked no-connect in the schematic).
- M5Unified finds it: `IMU_Class.inl:77-90` tries MPU6886, then BMI270 at 0x68, then 0x69. There is no ADV axis remap (`IMU_Class.inl:93-131`).
- `BMI270_Class.inl:38-91`: soft reset, uploads the 8 KB feature config, leaves accel and gyro on with power save off.
  `IMU_Base.hpp:74`: `sleep()` returns false and BMI270 does not override it. `internal_imu` defaults to true (`M5Unified.hpp:139`).
- No pin interrupt, so no wake on motion from deep sleep. Features (any-motion, no-motion, step counter) work by polling INT_STATUS registers.
- Orientation: accel sign can drive the 180 degree flip (rotation 1 or 3). Measure the axis mapping on a unit first.

## Buttons, sleep and current

- Buttons: BTN1 is RESET (EN). BTN2 is G0. M5Unified reads G0 as BtnA (`M5Unified.inl:2464-2467`, `:3571-3575`).
- Download mode: switch OFF, hold G0, connect USB, release.
- `M5.Power.powerOff()` on the ADV: `pmic_adc`, no power-hold pin, `_wakeupPin = 255` (`Power_Class.hpp:311`).
  `_powerOff()` (`Power_Class.inl:1595-1783`) calls `esp_deep_sleep_start()` with no wake source. Only RESET or a power cycle wakes it.
  Both rails stay up, so the amp, codec, IMU, keyboard and SD card keep drawing current.
- Deep sleep wake candidates: G0 (ext0, low), G11 keyboard INT (ext1 ANY_LOW, after the TCA8418 FIFO is flushed), timer.
  G38 and G44 are not RTC pins; use `gpio_hold_en` with `gpio_deep_sleep_hold_en` to hold them low.
- M5 figures at 4.2 V: switch OFF 0.23 uA, operating 120.2 mA, Wi-Fi 132.3 mA, BLE 154.6 mA. No deep sleep figure is published.
- I2C ports: `In_I2C` is I2C_NUM_1 on G8/G9, `Ex_I2C` is I2C_NUM_0 on G2/G1 (`M5Unified.inl:110`, `:2158-2175`).
- Variant `m5stack_stamp_s3/pins_arduino.h` sets SDA 13, SCL 15, TX 43, RX 44, and SPI pins -1. None match the ADV internal bus.
- The IDF console is UART0 (`CONFIG_ESP_CONSOLE_UART_NUM 0`), so ROM boot text toggles G43 at reset. This is harmless.

## Firmware notes

Gaps found in `firmware/src` and recommendations:

1. RGB LED power (`hal/system.cpp:37-45`, `core/app.cpp:38`, `core/app.cpp:430`). `setLed` now uses `neopixelWrite(21, ...)`.
   - Screen off sets brightness 0, which cuts LED power. The screen-off LED dimming in `updateLed` cannot show anything.
   - `setLed` skips equal colors (`if (c == led_) return;`). After any backlight-off period the LED stays dark until the color changes.
   - Below brightness 255 the LED supply is chopped at 256 Hz.
   - Fix: re-send the color after each `setBrightness` call and about every 100 ms. Skip sends at brightness 0 (G21 idles low).
     Decide the product rule for notifications with the screen off: keep the backlight at 255 with the panel in SLPIN, or accept no LED.
2. Charging state (`hal/system.cpp:21`, `core/app.cpp:220`). `isCharging()` is always `charge_unknown`, so `chg_` is always false.
   "plugged" is only true when the level is -1. Low-battery saver and auto-off (`core/app.cpp:232-242`) can act on USB power.
   - Fix: treat `HWCDC::isPlugged()` (USB host SOF seen) as external power. Add a voltage-trend rule for wall chargers.
     Show "USB" instead of a percent while plugged.
3. Battery percent (`hal/system.cpp:18-20`). Two single ADC samples per 5 s, and a linear 3.30 to 4.10 V curve.
   - Fix: read `getBatteryVoltage()` once, average 8 to 16 samples, filter (EMA), and map with a LiPo table.
     Typical rest values to tune: 4.20 V 100, 4.10 V 90, 3.98 V 75, 3.87 V 55, 3.80 V 40, 3.73 V 25, 3.65 V 10, 3.50 V 3, 3.30 V 0.
4. Power off (`hal/system.cpp:107-111`, `screens/prefs.cpp:259`). The text says "Press the side button to turn it back on".
   The G0 button cannot wake it, because no wake source is armed.
   - Fix: arm ext0 on G0 (and optionally ext1 on G11), call `M5.Speaker.end()`, power down the ES8311, suspend the BMI270
     (0x69: reg 0x7D = 0x00, then 0x7C = 0x03), and hold G38/G44 low. Tell the user that the side switch OFF is the real off.
5. Keyboard drain (`main.cpp:71-72`, `main.cpp:86`). One TCA8418 event per loop, with a 20 ms delay when the screen is off.
   - Fix: call `M5Cardputer.Keyboard.updateKeyList()` and `keyboard.poll()` in a loop of up to 10 passes, and stop when the key list stops changing.
     One event per pass keeps press and release visible to the diff logic in `hal/keyboard.cpp:42-73`.
6. IMU and RTC (`main.cpp:33-36`). The firmware does not use the BMI270, but M5.begin uploads its config and leaves accel and gyro on.
   - Fix: set `cfg.internal_imu = false` and `cfg.internal_rtc = false` (the ADV has no RTC chip). Then write the BMI270 suspend
     registers once at boot, because the chip keeps its state across an ESP32 reset.
7. Codec idle (`hal/audio.cpp:12`, `:52`, `:77`). The ADV speaker disable list is empty, so the DAC and amp stay on when idle.
   - Fix: after long idle and before power off, run `M5.Speaker.end()` and write the same power-down registers as the mic disable
     (0D=FC, 00=00). `Speaker.begin()` restores them. Compare with the ES8311 register map first.
8. Mic gain (`hal/audio.cpp:55-58`). ADV mic init sets the analog PGA to 0 dB, and the firmware adds digital `magnification`.
   - Fix: after each `M5.Mic.begin()`, write ES8311 reg 0x14 = 0x17 (+21 dB) via `M5.In_I2C.writeRegister8(0x18, 0x14, 0x17, 400000)`.
     Then lower `magnification`. If the first capture after boot is silent, re-write 0x0D = 0x01 after I2S starts (`M5Unified.inl:528-556`).
9. SD clock and bus (`hal/ota.cpp:8`, `:49`). 25 MHz is at the GPIO matrix limit, and `sdReady()` remounts on every call.
   - Fix: on mount failure retry at 20 MHz, then 10 MHz. Set G5 to `INPUT_PULLUP` (or output high) at boot so a Cap module
     on the header stays off the shared MISO line.
10. Display sleep. Screen off only sets brightness 0, and the ST7789 keeps running.
    - Fix: use `M5.Display.sleep()` and `M5.Display.wakeup()`, then apply the LED rule from item 1.
11. G44 at boot. Set G44 as output low until IR is used. This removes the pull-up path through the IR LED and any UART0 RX setup.
12. Pin hygiene. Do not call `Serial0.begin()` or `Wire.begin()` without pins. Default UART0 (G43/G44) and default I2C (G13/G15) are wrong here.
    Use `M5.In_I2C` and `M5.Ex_I2C`, not `Wire` or `Wire1`, because those ports are already taken.
13. G0 is both PTT (`main.cpp:73-74`) and the boot strap. Holding PTT while pressing RESET enters download mode. This is expected.

## Sources

- Cardputer-Adv product page: https://docs.m5stack.com/en/core/Cardputer-Adv
- Cardputer-Adv schematic: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf
- Stamp-S3A product page: https://docs.m5stack.com/en/core/Stamp-S3A
- Stamp-S3A schematic: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1150/Sch_StampS3_v0.3.3.pdf
- Stamp-S3A Arduino LED example (G21 data, G38 enable, GRB): https://docs.m5stack.switch-science.com/zh_CN/arduino/m5stamps3a/program
- M5 ADV demo firmware: https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV (`main/hal/hal_config.h`, `main/hal/hal.cpp`, `main/hal/utils/ir_nec/ir_helper.c`)
- ESP-IDF 4.4.7 SPI master limits: https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/spi_master.html
- TP4057 datasheet: https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2312291747_YONGYUTAI-TP4057_C20199358.pdf
