## Photo 1

### Scope and orientation

- Source: `photo-1.jpg`, a 1500 x 2000 pixel portrait photograph.
- The supplied identification is M5Stack Cardputer-Adv, K132-ADV, with a StampS3A / ESP32-S3FN8. Visible names include `CARDPUTER`, `ADV`, `STAMP S3A`, and `ESP32 S3FN8`. `K132-ADV` is not readable on this face.
- The device is held horizontally, front toward the camera. The display and module occupy the upper section; the keyboard occupies the lower section.
- Directions below refer to normal keyboard-reading orientation. Rotate the photograph 90 degrees clockwise to read the module label upright.
- The cream/off-white enclosure is a wide rectangle with rounded lower corners, chamfered upper corners, and a raised upper section. The front is almost parallel to the camera, with slight perspective distortion.
- A hand supports the left side and bottom. The back, underside, and most side surfaces are hidden. Room furnishings and clothing form the background.

### Front panel and enclosure

- Upper left: a recessed silver-gray panel, about the height of the display, with four cyan corner marks. It reads `CARDPUTER` above large bold `ADV`.
- Below `ADV`: a microphone graphic with a red/orange vertical capsule and black curved support and base. No microphone component is resolved at that position.
- Two narrow diagonal openings appear in the cream plastic below that panel, near a screw. Their internal parts are hidden.
- Upper center-left: a small landscape color display with a silver-gray inner border and cream outer recess.
- Between the screen and module: a large molded `M5` logo in the cream plastic.
- Upper right: a Stamp module with clipped corners, a black perimeter, and a silver/light-colored label covered in small markings.
- Two exposed metal screw heads secure the upper face: one below the identification panel, the other below/right of the molded `M5`, just left of the module. Their centers are recessed, but the driver profile is unclear.
- The right enclosure edge has openings beside the module. Dark internal material and short parallel metallic contact-like features are visible. The view does not show a connector mouth clearly enough to identify its type.
- The lower panel contains four recessed keyboard legend strips, each above a row of black buttons. Thin cream ribs separate the rows.
- No cable is visibly attached. The metal object below the device belongs to the zipper on the clothing in the background.

### Screen and visible firmware state

The screen is lit. It uses a blue interface with cyan title and footer bands. Visible text, from top to bottom:

```text
Offline
Wi-Fi
Not connected

No networks found. Press r.

ent join  r scan  h hidden  x forget
```

- A small round status symbol precedes `Offline`; its exact design is unclear.
- At top right are faint ascending bars, `46`, and a battery-outline icon with a cyan interior. No percent sign is clearly readable.
- `Wi-Fi` is the larger title. `Not connected` appears immediately below the cyan title band.
- The no-networks message is centered in the lower part of the main blue area.
- Footer spacing above is normalized. The screen uses `ent`; the physical Enter key uses `ok`.
- The active screen area is about 400 x 240 pixels in this photograph. The panel's native resolution, controller, bus, and rotation setting cannot be established from the photo.

### Keyboard construction and coordinate convention

There are 56 black buttons arranged in four aligned rows of 14. The columns are straight, with no desktop-style row staggering. Tab, Enter, Space, and the modifiers each occupy one position and appear the same size as letter buttons.

Each button is a small, raised, horizontally elongated rounded rectangle with a textured surface. Legends are printed on the cream strip above the buttons. The black button surfaces have no readable printing. A button is about 60 pixels wide by 37 pixels high in the photograph. Centers are about 92 pixels apart horizontally and 118 pixels apart vertically. These are image measurements; no physical scale is provided.

The tables use these physical coordinates:

- Rows `R1` through `R4`, counted downward.
- Columns `C01` through `C14`, counted toward the right.

These coordinates describe placement. They do not establish electrical matrix rows, columns, scan order, or GPIO assignments.

Main characters are black. Secondary punctuation is smaller and dark blue, usually above and to the right. Orange/red marks identify `esc`, `del`, `fn`, and directional triangles. `Aa` has a blue background; `opt` has a green/teal background. The photo does not establish which modifier activates each colored legend or secondary character.

Letter legends are uppercase in print. Their printed case does not establish the case emitted by firmware. `None visible` below means no separate legend is visible at that position. All secondary punctuation in the tables is dark blue.

### Row 1: number and editing row

| Position | Main legend | Secondary legend | Colored/function legend |
| --- | --- | --- | --- |
| R1C01 | Backtick/grave accent (ASCII 0x60) | `~` | White lowercase `esc` on orange/red, above the punctuation. |
| R1C02 | `1` | `!` | None visible. |
| R1C03 | `2` | `@` | None visible. |
| R1C04 | `3` | `#` | None visible. |
| R1C05 | `4` | `$` | None visible. |
| R1C06 | `5` | `%` | None visible. |
| R1C07 | `6` | `^` | None visible. |
| R1C08 | `7` | `&` | None visible. |
| R1C09 | `8` | `*` | None visible. |
| R1C10 | `9` | `(` | None visible. |
| R1C11 | `0` | `)` | None visible. |
| R1C12 | `_`, long black low stroke | `-`, short dark-blue high stroke | None visible. |
| R1C13 | `=` | `+` | None visible. |
| R1C14 | White left arrow on black | None visible | White lowercase `del` on an orange/red upper-right patch; Backspace/Delete position. |

R1 sits directly below the display and module. R1C12 is recorded by appearance: the black stroke is lower and longer than the blue one. The actual minus/underscore modifier mapping needs confirmation in firmware. R1C14 has a straight left arrow, distinct from the orange navigation left arrow at R4C11 and the bent Enter arrow at R3C14.

### Row 2: Tab and QWERTY row

| Position | Main legend | Secondary legend | Colored/function legend |
| --- | --- | --- | --- |
| R2C01 | `tab` | None visible | Black lowercase text. |
| R2C02 | `Q` | None visible | None visible. |
| R2C03 | `W` | None visible | None visible. |
| R2C04 | `E` | None visible | None visible. |
| R2C05 | `R` | None visible | None visible. |
| R2C06 | `T` | None visible | None visible. |
| R2C07 | `Y` | None visible | None visible. |
| R2C08 | `U` | None visible | None visible. |
| R2C09 | `I` | None visible | None visible. |
| R2C10 | `O` | None visible | None visible. |
| R2C11 | `P` | None visible | None visible. |
| R2C12 | `[` | `{` | None visible. |
| R2C13 | `]` | `}` | None visible. |
| R2C14 | Backslash (ASCII 0x5C) | Vertical bar (ASCII 0x7C) | None visible. |

### Row 3: Fn, Aa/Shift, home letters, and Enter

| Position | Main legend | Secondary legend | Colored/function legend |
| --- | --- | --- | --- |
| R3C01 | `fn` | None visible | White lowercase text on an orange/red rectangle. |
| R3C02 | `Aa` | None visible | White text on a blue rectangle. Aa/Shift position; the word `shift` is not printed. |
| R3C03 | `A` | None visible | None visible. |
| R3C04 | `S` | None visible | None visible. |
| R3C05 | `D` | None visible | None visible. |
| R3C06 | `F` | None visible | None visible. |
| R3C07 | `G` | None visible | None visible. |
| R3C08 | `H` | None visible | None visible. |
| R3C09 | `J` | None visible | None visible. |
| R3C10 | `K` | None visible | None visible. |
| R3C11 | `L` | None visible | None visible. |
| R3C12 | `;` | `:` | Orange/red up arrow: a filled upward triangle behind the semicolon. |
| R3C13 | Apostrophe/single quote (ASCII 0x27) | Double quote (ASCII 0x22) | The printed quote strokes are curved. |
| R3C14 | `ok` and a bent left arrow | None visible | White lowercase `ok` above a white return/Enter arrow on a black rectangle. |

The Enter arrow bends down from the right and then points left. Its button is the same single-column size as the buttons beside it.

### Row 4: modifiers, bottom letters, navigation, and Space

| Position | Main legend | Secondary legend | Colored/function legend |
| --- | --- | --- | --- |
| R4C01 | `ctrl` | None visible | Black lowercase text. |
| R4C02 | `opt` | None visible | Black lowercase text on a green/teal rectangle. |
| R4C03 | `alt` | None visible | Black lowercase text. |
| R4C04 | `Z` | None visible | None visible. |
| R4C05 | `X` | None visible | None visible. |
| R4C06 | `C` | None visible | None visible. |
| R4C07 | `V` | None visible | None visible. |
| R4C08 | `B` | None visible | None visible. |
| R4C09 | `N` | None visible | None visible. |
| R4C10 | `M` | None visible | None visible. |
| R4C11 | `,` | `<` | Orange/red left arrow: a filled left-pointing triangle behind the comma. |
| R4C12 | `.` | `>` | Orange/red down arrow: a filled downward triangle behind the period. |
| R4C13 | `/` | `?` | Orange/red right arrow: a filled right-pointing triangle behind the slash. |
| R4C14 | Space symbol | None visible | Black horizontal bar with short upward ends, forming a shallow open-top bracket. The word `space` is not printed. |

Space is the bottom-right button, directly below Enter. There is no long central spacebar. The four orange navigation legends form an inverted T: up at R3C12, left at R4C11, down at R4C12, and right at R4C13.

### Physical layout for key mapping

The map below uses descriptive names for symbol keys. Refer to the tables for their printed legends.

```text
       C01   C02 C03 C04 C05 C06 C07 C08 C09 C10 C11 C12 C13   C14
R1     grave 1   2   3   4   5   6   7   8   9   0   _   =     backspace
R2     tab   Q   W   E   R   T   Y   U   I   O   P   [   ]     backslash
R3     fn    Aa  A   S   D   F   G   H   J   K   L   ;   quote enter
R4     ctrl  opt alt Z   X   C   V   B   N   M   ,   .   /     space
```

- C01 aligns grave/Escape, Tab, Fn, and Ctrl.
- C02 aligns `1`, `Q`, `Aa`, and `opt`.
- C12 aligns underscore/minus, left bracket, semicolon/up arrow, and period/down arrow.
- C14 aligns Backspace/Delete, backslash/vertical bar, Enter/OK, and Space.
- No separate function-key row, number pad, Caps Lock key, or navigation buttons outside the punctuation positions are visible.
- The keyboard PCB, switch contacts, traces, and scan controller are hidden. The physical 4 x 14 arrangement does not establish the electrical matrix dimensions or scan order.

### Module label and printed pin references

For this section, read the module label upright by rotating the photograph 90 degrees clockwise. Left and right here refer to that rotated label.

The top reads `STAMP`, red `S3A`, `ESP32`, blue `S3FN8`, and `3D-ANT`. A red square contains a white Espressif-style emblem. A dotted border surrounds this area.

Below it are these visible markings:

- `Model Name: M5Cardputer`.
- A small FCC line that appears to read `FCC ID:2AN3WM5CARDPUTER`. Some characters are blurred; the exact identifier needs confirmation from a sharper image.
- The blue/black `M5 STACK` logo.
- A circular certification symbol, a boxed `R`, and `219-249271`.
- A crossed-out wheeled-bin symbol.
- `BTN`, with smaller `DOWNLOAD` and `MODE`, next to a large white `0` in an orange/red circle. This group has a black pill-shaped background.
- Large FCC and CE-style conformity symbols.
- Small text reading `M5Stack Technology Co., Ltd`, split over short lines.
- Large `USB`, beside `D-:G19` and `D+:G20`, with white `OTG/SERIAL` on a pink/red strip below.

The `BTN 0 DOWNLOAD MODE` marking identifies a button-related area. The photograph does not clearly separate a moving button cap from the printed cover. This area is separate from the keyboard buttons.

Along the left label edge, staggered blue circles contain `1` through `10`, beside vertical `GPIO+ADC` text. Below them:

| Group | Visible marking |
| --- | --- |
| Ground | White `GND` on black. |
| GPIO 11 | `GPIO` beside blue/teal `11`. |
| Power | White `5V` on red. |
| GPIO 12 | `GPIO` beside blue/teal `12`. |
| I2C marking | Yellow-backed `13` with `SDA`. |
| GPIO 14 | `GPIO` beside blue/teal `14`. |
| I2C marking | `15` with `SCL`. |

Along the right label edge, from top to bottom:

| Group | Visible marking |
| --- | --- |
| Power | White `3V3` on red. |
| GPIO 46 | Blue/teal `46` with `GP`. |
| UART TX | Small `U0 TX` beside blue-outlined `43`; the small letters are cramped. |
| Serial audio | Yellow-backed `42` with `MCK`. |
| UART RX | `U0Rx` beside blue-outlined `44`. |
| Serial audio | Yellow-backed `41` with `MDI`. |
| Reset | `RST` on pink beside white `EN` in a darker field. |
| Serial audio | Yellow-backed `40` with `MDO`. |
| GPIO 0 | `GPIO` on pink beside white `0` on orange/red. |
| Status area | `G21`, colored `RGB`, and `STATUS`, near yellow-backed `39`. Their proximity does not establish a shared function. |
| Ground | White `GND` on black, with a curved black line extending toward the right label edge. |

At the bottom is a dotted-outline pin diagram with pink bands labeled `Ext.` and `LCD-PORT`. Its entries are printed vertically in very small text. Visible fragments include `1-5V`, `6-GND`, `7-G33/RST`, `11-3V3`, and `12-G37/CS`. Other entries contain blurred GPIO numbers and signal abbreviations. The full diagram cannot be transcribed with confidence from this photograph. Confirm its connector pinout against a sharper source before using it.

The numbered colored circles are printed references. No exposed header holes or lit LEDs are resolved at those circles. The photo does not show which printed GPIO references connect to the keyboard.

### LEDs, buttons, ports, and unresolved details

- No separate lit status LED is clearly visible. The module has an `RGB STATUS` marking, but the light-emitting part and its state are unresolved.
- The red/orange microphone symbol and colored pin labels are printed graphics. Their color does not establish that an LED is on.
- All 56 keyboard buttons and the module's `BTN 0 DOWNLOAD MODE` marking are visible. No other side button or power switch is clearly identifiable from this angle.
- The USB labels are readable, but the receptacle mouth is not clearly shown. Its connector shape cannot be confirmed from this photograph.
- The short parallel metallic features in the right-edge opening cannot be assigned a connector type or reliable contact count from this view.
- No microSD slot, audio jack, battery label, rear expansion connector, or separate sensor is clearly identifiable on the visible face. Other surfaces are outside the image's coverage.
- The photograph does not reveal board revision, firmware version, electrical matrix wiring, pull-up configuration, rollover behavior, debounce timing, or modifier combinations for the colored legends.
- The grave accent is a short slanted mark; the quote marks are curved; Space and Enter use symbols. Their ASCII names above preserve the transcription without Unicode glyphs.
