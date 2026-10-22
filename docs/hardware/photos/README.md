# Cardputer-Adv reference photos

Photos of the device this firmware targets: M5Stack Cardputer-Adv (K132-ADV), Stamp-S3A module, ESP32-S3FN8.
Three Codex agents (GPT-6-Astra, high reasoning) wrote the descriptions, one agent per photo.

| Photo | Shows | Description |
| --- | --- | --- |
| [photo-1.jpg](photo-1.jpg) | Front: display, Stamp-S3A label with pin markings, and all 56 keys with their legends | [photo-1.md](photo-1.md) |
| [photo-2.jpg](photo-2.jpg) | Device above its retail box (K132-ADV) | [photo-2.md](photo-2.md) |
| [photo-3.jpg](photo-3.jpg) | Back label: block diagram with every chip and GPIO | [photo-3.md](photo-3.md) |

## Facts used by the firmware

- Keys sit in a 4 x 14 grid. Esc is on the backtick key (top left) and del on the top right. ok (Enter) is on row 3 at the right, with Space under it.
- Arrows are printed on `;` (up), `,` (left), `.` (down) and `/` (right). The left column holds fn, Aa (shift), ctrl, opt and alt.
- The Stamp-S3A label prints `G21 RGB STATUS`: the WS2812 status LED is on GPIO 21. `BTN 0` is the download button on GPIO 0.
- The back label gives the pins in [../README.md](../README.md): ES8311 on G46/G43/G41/G42, TCA8418 on I2C G8/G9 with INT G11, BMI270 on the same bus, battery sense at half voltage on G10, LCD on G33 to G38, USB on G19/G20.
