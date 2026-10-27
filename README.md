# M5Stack PaperDIY Arduino Examples

Arduino sketches for the [M5Stack PaperDIY](https://docs.m5stack.com/en/arduino/paperdiy/program) (ESP32-S3, 4.7" e-paper, two buttons, microSD, PM1 power chip).

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
| [PaperDIYUserDemo](PaperDIYUserDemo) | Port of the ESP-IDF user demo: battery, Wi-Fi scan, TF card speed test | Works |
| [EpubReader](EpubReader) | EPUB reader, port of [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader). See its [README](EpubReader/README.md) | Works |
| [FastEPD/animated_smiles](FastEPD/animated_smiles) | Animation with [FastEPD](https://github.com/bitbank2/FastEPD) | **Does not work** (see below) |

## Known issue: FastEPD examples

The FastEPD examples **do not work right now**. The current FastEPD release has no definition for the PaperDIY panel, so `initPanel()` / `setPanelSize()` can't drive this display. The sketch is kept in the repo for reference and will be revisited when FastEPD adds support for the panel.

Use M5Unified / M5GFX (as in all the other sketches) instead.

## Setup

Board: `m5stack:esp32:m5stack_paperdiy`, PSRAM: **OPI PSRAM**.

Install from the Library Manager:

- **M5Unified**
- **M5GFX**
- **M5PM1** (only for the `M5PM1` sketch)

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
