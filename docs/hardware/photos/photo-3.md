## Photo 3

Source: `photo-3.jpg`, 1500 x 2000 pixels. This reference transcribes the rear label as photographed. Signal spelling and GPIO numbers below follow the printing. Connections describe the printed block diagram; it does not show a complete electrical schematic.

### Device and physical layout

The photograph shows the back of a dark gray handheld enclosure, held horizontally. A cream-colored label covers most of the back. Four large circular mounting recesses run across the upper edge, with two smaller brass-colored threaded inserts between them. Two metal screw heads sit within dark circular recesses inside the label area. The lower edge has an inset molded `CARDPUTER` name and a blue diagonal `ADV` label next to it.

The diagram places the audio circuitry at upper left, the expansion header at top center, battery and power circuitry in the upper middle, the external four-pin port at upper right, the controller at lower left, the keyboard controller and matrix across the lower middle, and the display at lower right. A blue-and-black `M5` / `STACK` logo occupies the bottom-right corner.

The controller block reads, on separate lines:

```text
STAMP
S3A
ESP32S3FN8
```

The last line is below the controller rectangle. The photograph prints `ESP32S3FN8` without a hyphen. The supplied device identification is Cardputer-Adv / K132-ADV, but `K132-ADV` is not visible on this label.

### Printed line legend

The bottom-right legend is a rounded rectangle with a dashed dark border. It contains a reddish curved line beside `POWER` and a blue straight line beside `DATA`.

Power paths elsewhere are reddish-orange. Data paths are blue, with some audio paths appearing dark purple and keyboard matrix lines appearing dark green or black. Arrowheads show the direction drawn; they do not establish electrical direction for every signal grouped on a line.

### Top expansion header

The header is drawn as a two-row, seven-column rectangle. Read left to right with the photograph upright:

| Position | Top row | Bottom row, preserving stacked text with `/` |
| --- | --- | --- |
| 1 | G3 | 5V / IN |
| 2 | G4 | GND |
| 3 | G6 | 5V / OUT |
| 4 | G40 | G8 / SDA |
| 5 | G14 | G9 / SCL |
| 6 | G39 | G13 |
| 7 | G5 | G15 |

`GND` is diagonal within its cell. The 5 V input and output cells have reddish backgrounds, SDA is green, and SCL is yellow. These are physical positions in the printed diagram; the photo provides no pin-1 marker or connector numbering. No connection lines extend from this header to the other blocks.

### Audio: microphone, codec, headphones, and speaker

The codec block reads `ES` above `8311`, giving the part name ES8311. A reddish arrow labeled `3V3` points into its right edge.

Above and to the right is `Mic` over a microphone symbol. A dark bent line runs from the microphone area down and left, ending in a left-pointing arrow at the ES8311 block.

The blue controller-to-codec path runs upward from the STAMP S3A area and ends in an upward arrow below ES8311. Its four printed signal labels, top to bottom, are:

```text
ASDOUT:G46
LRCK:G43
SCK:G41
DSDIN:G42
```

The printed spellings are `ASDOUT` and `DSDIN`. No MCLK label or MCLK GPIO is visible.

At the far upper left is a stylized audio plug/contact drawing, with green, dark gray, brown, and yellow-orange sections. Its labels, left to right, are `GND`, `OUT_LR`, and `HP_DET`. Small pointer marks associate the labels with contacts. `OUT_LR` has two upward pointers. A headphone symbol sits below, beside the two-line red text:

```text
Output
Only
```

Below ES8311 is an amplifier block reading `NS` above `4150`, giving NS4150. A dark line descends from the codec area, bends left, and ends in a left-pointing arrow at NS4150. The line is unlabeled. A reddish `3V3` arrow points into the amplifier from its right.

`HP_DET` has a downward-pointing triangle and small circle below it, drawn as an inversion symbol. A blue line continues downward from that symbol to the top of NS4150. No GPIO is printed for headphone detection.

A short horizontal dark line connects NS4150's left edge to a speaker symbol. Beside and below that symbol are `1W` and `SPEAKER`. The photo does not print speaker impedance. The plug/headphone drawing has no clearly drawn continuous connection to ES8311; the picture does not establish other connector nets.

### USB and controller

The left-side USB illustration is labeled `USB` above a large `C`, followed by a connector outline. Beneath it is `OTG`.

A blue vertical path between the USB illustration and STAMP S3A has arrowheads toward the USB end and toward the controller end. Beside it are:

```text
D-:G19
D+:G20
```

A reddish path labeled `5VIN` leaves the USB connector to the right and continues toward the charger input. A `3V3` arrow points downward into the nearby BMI270 block; it is a separate supply marking.

### Motion sensor

The motion-sensor block reads `BMI` above `270`, giving BMI270, beside a small circular motion/axis symbol. Its supply label is `3V3`. Beneath it are:

```text
SCL:G9
SDA:G8
```

A short blue bent line runs downward from the sensor area toward STAMP S3A, ending in a downward arrow. No BMI270 interrupt pin is printed.

### Keyboard controller and matrix

The keyboard-controller block reads `TCA` above `8418`, giving TCA8418. A reddish `3V3` arrow points into its top edge. The pin labels to its lower right are:

```text
SCL:G9
SDA:G8
INT:G11
```

A blue bent path connects the bottom of TCA8418 to the right side of STAMP S3A. It has an upward arrow at the keyboard-controller end and a left-pointing arrow at the STAMP end.

Two dark lines fan from the TCA8418 area toward the matrix: an upper diagonal line ends near `Y0`, and a lower line ends near `Y7`. Intermediate individual connections are not drawn.

The grid has seven vertical lines, labeled left to right `C0`, `C1`, `C2`, `C3`, `C4`, `C5`, `C6`, and eight horizontal lines, labeled top to bottom `Y0`, `Y1`, `Y2`, `Y3`, `Y4`, `Y5`, `Y6`, `Y7`. Each vertical line crosses each horizontal line. The crossings do not have individually drawn key switches or connection dots.

The caption under the grid reads:

```text
KEY MATRIX : 4x14
```

The drawn grid is 7 by 8, while the caption says `4x14`. Both represent 56 positions, but the photo does not define the relationship between physical key placement and the controller's row/column wiring. C0-C6 and Y0-Y7 are matrix labels, not printed ESP32 GPIO assignments.

### Battery, charging, switching, and regulators

At top center, the text `SWITCH` sits over a blue switch illustration with a right-pointing arrow and a white circle marked `ON`. A blue line leaves the switch's right side, bends downward, and points into an oval marked `MOS`.

To the right is a battery symbol and the following text:

```text
BAT
1750mAh
*INSIDE
```

The photograph gives capacity and internal placement. It does not print battery voltage, chemistry, cell count, or a battery part number.

The charging block reads `CHG-IC` above `TP4057`. The USB `5VIN` power path ends in a right-pointing arrow at this block. A power path labeled `BAT-IN` leaves its right edge.

The power and control connections visible in this area are:

1. The upper `MOS` below the ON switch connects to the battery with a curved reddish path. Arrowheads are visible toward the battery and toward the MOS end.
2. Another curved reddish path connects that upper MOS to the `BAT-IN` area below it. Arrowheads are visible toward the upper MOS and toward the lower BAT-IN area.
3. The TP4057 output path labeled `BAT-IN` curves upward and right to a second oval marked `MOS`. A right-pointing arrow enters this second MOS.
4. A resistor-divider branch descends from the BAT-IN-side curved path. It contains two unnumbered rectangular resistor symbols in series and a ground symbol at the bottom. The photograph prints no resistor values.
5. A blue line from the divider midpoint runs left, then diagonally down and left to STAMP S3A. It is labeled `1/2VBAT:G10` and ends with an arrow toward the controller.
6. Below the second MOS is a black block reading `CN` above `809`, giving CN809. A blue upward arrow connects its top to the bottom of that MOS. A reddish upward arrow labeled `5VIN` enters CN809 from below.
7. A reddish line leaves the second MOS to the right, with `BAT_OUT` printed above it. It continues toward the DC/DC block labeled `SY7088`.
8. A reddish branch rises from the BAT_OUT region, curves right, and enters the upper DC/DC block labeled `SY8089`.
9. A second reddish `5VIN` path runs right from beneath CN809, curves upward through a black diode symbol, and reaches the BAT_OUT region with an upward arrow. The diode has its bar at the upper end. Its part number is not printed.

The upper regulator block reads:

```text
DC/DC
SY8089
```

A reddish right-pointing arrow leaves it beside `3V3`.

The lower regulator block reads:

```text
DC/DC
SY7088
```

A reddish right-pointing arrow leaves it beside the stacked label `5V` / `OUT`.

The same `3V3` supply label is repeated at ES8311, NS4150, BMI270, TCA8418, and the display. The label does not draw a continuous wire from SY8089 to each of those consumers. It also does not print MOS part numbers or GPIO assignments for the ON switch or either MOS control.

### Right-side four-pin port and supply selection

The narrow vertical port legend has four stacked cells. Read top to bottom:

```text
GND
*5V
G2
G1
```

The ground cell is dark, the supply cell reddish, G2 yellow, and G1 pale gray. No port name, signal function for G2/G1, or connector pin numbering is printed.

Below the cells, the markings read top to bottom:

```text
5VOUT
[upward triangle]
SW
[downward triangle]
5VIN
```

The bracketed lines describe symbols, not literal text. Two curved reddish traces run beside the stack and supply-selection marks. Their arrowheads meet the `*5V` area and the `5VIN`/`5VOUT` switch area. The drawing associates the starred port supply with the two selectable rail labels, but the small trace junctions and exact switch-contact wiring are not fully resolved in the photo. There is no continuous drawn line from this port to the controller or regulator blocks.

### Display

The display block has a reddish `3V3` arrow pointing toward its upper-left side. The block reads:

```text
1.14"
IPS-LCD
ST7789V2
```

The signal labels are printed beneath it in three rows and two columns:

| Left column | Right column |
| --- | --- |
| DATA:G35 | RST:G33 |
| SCK:G36 | CS:G37 |
| RS:G34 | BL:G38 |

The photo labels the display data line `DATA`, the register-select line `RS`, and the backlight line `BL`. No display resolution, SPI mode, reset polarity, backlight polarity, or bus timing is printed. There are no drawn data lines from the display to STAMP S3A.

### Signal, GPIO, Part

GPIO numbers in this table are transcribed from the printed `G` labels. `Unspecified` means the photo gives a GPIO without a peripheral signal name. `Not printed` means no ESP32 GPIO is supplied for that label. The table repeats GPIOs for each labeled part or connector position.

| Signal | GPIO | Part |
| --- | --- | --- |
| ASDOUT | G46 | ES8311 |
| LRCK | G43 | ES8311 |
| SCK | G41 | ES8311 |
| DSDIN | G42 | ES8311 |
| D- | G19 | USB C / STAMP S3A |
| D+ | G20 | USB C / STAMP S3A |
| SCL | G9 | BMI270 |
| SDA | G8 | BMI270 |
| SCL | G9 | TCA8418 |
| SDA | G8 | TCA8418 |
| INT | G11 | TCA8418 |
| 1/2VBAT | G10 | Battery divider / STAMP S3A |
| DATA | G35 | ST7789V2 |
| RST | G33 | ST7789V2 |
| SCK | G36 | ST7789V2 |
| CS | G37 | ST7789V2 |
| RS | G34 | ST7789V2 |
| BL | G38 | ST7789V2 |
| Unspecified, top position 1 | G3 | Top expansion header |
| Unspecified, top position 2 | G4 | Top expansion header |
| Unspecified, top position 3 | G6 | Top expansion header |
| Unspecified, top position 4 | G40 | Top expansion header |
| Unspecified, top position 5 | G14 | Top expansion header |
| Unspecified, top position 6 | G39 | Top expansion header |
| Unspecified, top position 7 | G5 | Top expansion header |
| SDA, bottom position 4 | G8 | Top expansion header |
| SCL, bottom position 5 | G9 | Top expansion header |
| Unspecified, bottom position 6 | G13 | Top expansion header |
| Unspecified, bottom position 7 | G15 | Top expansion header |
| Unspecified, third cell | G2 | Right-side four-pin port |
| Unspecified, fourth cell | G1 | Right-side four-pin port |
| 5V IN, bottom position 1 | Not printed | Top expansion header |
| GND, bottom position 2 | Not printed | Top expansion header |
| 5V OUT, bottom position 3 | Not printed | Top expansion header |
| GND, first cell | Not printed | Right-side four-pin port |
| *5V, second cell | Not printed | Right-side four-pin port |
| GND | Not printed | Audio plug/contact drawing |
| OUT_LR | Not printed | Audio plug/contact drawing |
| HP_DET | Not printed | Headphone-detection path to NS4150 |
| C0 | Not printed | Keyboard matrix / TCA8418 |
| C1 | Not printed | Keyboard matrix / TCA8418 |
| C2 | Not printed | Keyboard matrix / TCA8418 |
| C3 | Not printed | Keyboard matrix / TCA8418 |
| C4 | Not printed | Keyboard matrix / TCA8418 |
| C5 | Not printed | Keyboard matrix / TCA8418 |
| C6 | Not printed | Keyboard matrix / TCA8418 |
| Y0 | Not printed | Keyboard matrix / TCA8418 |
| Y1 | Not printed | Keyboard matrix / TCA8418 |
| Y2 | Not printed | Keyboard matrix / TCA8418 |
| Y3 | Not printed | Keyboard matrix / TCA8418 |
| Y4 | Not printed | Keyboard matrix / TCA8418 |
| Y5 | Not printed | Keyboard matrix / TCA8418 |
| Y6 | Not printed | Keyboard matrix / TCA8418 |
| Y7 | Not printed | Keyboard matrix / TCA8418 |

### Remaining markings and limits of the photograph

- A small horizontal red dash appears near the lower edge, beneath the TCA8418 pin-label area. It has no text beside it or drawn connection.
- The microphone, speaker, headphones, USB connector, battery, switch, ground, diode, resistors, and motion/axis icon are pictorial symbols; the photo supplies no other component identifiers for them.
- The audio plug's smallest pointer endpoints and the right-side supply selector's exact line junctions are unclear. The contact labels are readable, but a complete contact-number mapping is absent.
- The resistor-divider branch is visible, but its junction with the curved BAT-IN path is small and lacks a clear junction dot. The `1/2VBAT:G10` annotation explicitly identifies the measured signal.
- The blue codec/controller path and nearby dark codec/amplifier path run close together. Their blocks and listed signals are readable; individual conductors within those grouped paths are not drawn.
- `CN809`, `ASDOUT`, `DSDIN`, `BAT-IN`, `BAT_OUT`, and `KEY MATRIX : 4x14` are transcribed as printed, without correcting them to alternate part or signal names.
- There is no visible serial number, QR code, regulatory text, firmware version, I2C address, or electrical pin numbering for the named IC packages.
