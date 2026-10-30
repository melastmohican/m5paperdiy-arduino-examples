# M5Stack PaperDIY Arduino Examples

Arduino sketches for the [M5Stack PaperDIY](https://docs.m5stack.com/en/arduino/paperdiy/program) (ESP32-S3, 4.7" e-paper, two buttons, microSD, PM1 power chip).

<img src="images/m5paperdiy.jpg" alt="M5Stack PaperDIY board" width="600">

Most sketches come straight from the official M5Stack
[PaperDIY Arduino program docs](https://docs.m5stack.com/en/arduino/paperdiy/program), with small tweaks along the way. `EpubReader` and `FastEPD` come from elsewhere (see below).

## Examples

| Sketch | What it does | Status |
| --- | --- | --- |
| [Button](Button) | Shows BtnA / BtnB presses on screen | Works |
| [Display](Display) | Basic M5GFX drawing and a border test | Works |
| [TextLogScroll](TextLogScroll) | Scrolling text log via M5GFX | Works |
| [microSD](microSD) | Reads `PaperDIY_01.png` / `PaperDIY_02.png` from the SD card and shows them | Works |
| [M5PM1](M5PM1) | Talks to the PM1 power-management chip (battery, power) | Works |
| [FactoryTest](FactoryTest) | Port of the M5Paper factory test: checklist, refresh-mode compare, gray ramp, Life game | Works |
| [PaperDIYUserDemo](PaperDIYUserDemo) | Port of the [ESP-IDF user demo](https://github.com/m5stack/M5PaperDIY-UserDemo): battery, Wi-Fi scan, TF card speed test | Works |
| [EpubReader](EpubReader) | EPUB reader, port of [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader). See its [README](EpubReader/README.md) | Works |
| [EpdFastCounter](EpdFastCounter) | Counter on the M5GFX `epd_fast` mode, reference for the Rust `epdpi` FastMono example | Works |
| [EpdModesCounter](EpdModesCounter) | Counter in the M5GFX `epd_fast`, `epd_fastest` and `epd_text` modes, five updates each, with a full `epd_quality` clear before each mode. Serial prints the time per update. Reference for the Rust `epdpi_m5_modes` example (on the `bringup` branch of `rust-m5paperdiy-examples`) | Works |
| [EpdQualityRamp](EpdQualityRamp) | 16-level gray ramp on the M5GFX `epd_quality` mode, reference for the Rust `epdpi` M5Quality example | Works |
| [FastEPD/animated_smiles](FastEPD/animated_smiles) | Eight smileys bounce for 250 frames with [FastEPD](https://github.com/bitbank2/FastEPD) partial updates | Works with FastEPD `main` (see below) |

## FastEPD example

[FastEPD/animated_smiles](FastEPD/animated_smiles) needs FastEPD from its **`main` branch**, not the 2.2.0 release (Apr 2026). The release has no panel definition that fits the PaperDIY and no `BBEP_DISPLAY_ED047TC1` (added to `main` on 15 Aug 2026), so the sketch as it came did not compile.

What the sketch does differently from FastEPD's own example:

- It initialises the panel with `BB_PANEL_M5PAPERS3`. The PaperDIY wires the ED047TC1 like the PaperS3: the same 8 data pins and control lines (see the pin table below), and that definition already carries the 960 x 540 size and the M5 waveform. `BB_PANEL_V7_RAW`, which the original example used, is the EPDiy pin map and is wrong for this board.
- It switches the panel supply on first. GPIO2 of the M5PM1 power chip gates it (I2C 0x6E on SDA 41 / SCL 42), and FastEPD does not know about it. The sketch does the same register writes as M5GFX at board detect, and switches it off at the end.

Checked on the board: the smileys bounce, and because the sketch never clears the screen each one leaves two long diagonal trails (the bits of outline that touch the edge of its square). After 250 frames the panel clears to white and powers down, and the blank screen shows no ghosting.

Build against a clone of FastEPD `main` without touching an installed copy:

```
git clone https://github.com/bitbank2/FastEPD ~/Src/tmp/FastEPD
arduino-cli compile --fqbn m5stack:esp32:m5stack_paperdiy:PSRAM=opi --library ~/Src/tmp/FastEPD FastEPD/animated_smiles
```

## Hardware notes

Panel: E Ink **ED047TC1**, 4.7", 540×960, 16 gray levels, 8-bit parallel interface
([datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/517/C139_ED047TC1_datasheet.pdf)).
Board design: [PaperDIY schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1288/K159-B_PaperDIY_sche.pdf).

**Panel connector:** 44 pins, 0.4 mm pitch. The panel has a Panasonic header
[AXE644224](https://industry.panasonic.com/global/en/products/control/connector/base-fpc/number/axe644224);
the board has the mating socket
[AXE544127](https://industry.panasonic.com/global/en/products/control/connector/base-fpc/number/axe544127) (`EPD1` in the schematic).

**ESP32-S3 pins to the panel** (as configured in [M5GFX](https://github.com/m5stack/M5GFX)):

| Panel signal | GPIO |
| --- | --- |
| D0–D7 | 6, 14, 7, 12, 9, 11, 8, 10 |
| XCL (clock) | 16 |
| XLE (latch) | 15 |
| XSTL / SPH (source start) | 13 |
| XOE (output enable) | 45 |
| CKV (gate clock) | 18 |
| SPV (gate start) | 17 |
| Panel power (boost converter) | 46, plus M5PM1 GPIO2 over I2C (SDA 41, SCL 42) |

### Libraries that do not drive this panel

- **GxEPD2** and **[GxEPD_HD](https://github.com/ZinggJM/GxEPD_HD)** cover SPI panels and Good Display / Waveshare HD panels. Neither has an ED047TC1 class. Online examples that use `GxEPD2_470_T75` with PaperDIY pins are wrong.
- **[LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47)** drives the same panel, but on different GPIOs and with a shift register for the control signals. It would need a pin remap and the M5PM1 power-up sequence.

## Setup

Board: `m5stack:esp32:m5stack_paperdiy`, PSRAM: **OPI PSRAM**.

Install from the Library Manager:

- **M5Unified**
- **M5GFX**
- **M5PM1** (only for the `M5PM1` sketch)
- **FastEPD** from its `main` branch, not the release (only for `FastEPD/animated_smiles`, see [FastEPD example](#fastepd-example))

SD, SPI and WiFi come with the ESP32 core.

## Build and upload

```
arduino-cli compile --fqbn m5stack:esp32:m5stack_paperdiy:PSRAM=opi Button
arduino-cli upload -p <port> --fqbn m5stack:esp32:m5stack_paperdiy:PSRAM=opi Button
```

Replace `Button` with any sketch folder.

## microSD sketch

Copy `PaperDIY_01.png` and `PaperDIY_02.png` from the repo root to the root of a FAT32 microSD card before running [microSD](microSD).

## License

Sketches derived from M5Stack docs and third-party projects keep their original licenses. See each project's source for details.
