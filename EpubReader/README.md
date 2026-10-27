# EpubReader (M5Stack PaperDIY)

Port of [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)
to Arduino + M5Unified / M5GFX. A PlatformIO version lives in
`PlatformIO/examples/m5paperdiy-epub-reader`.

Board: `m5stack:esp32:m5stack_paperdiy`, PSRAM: OPI PSRAM.

```
arduino-cli compile --fqbn m5stack:esp32:m5stack_paperdiy:PSRAM=opi EpubReader
arduino-cli upload -p <port> --fqbn m5stack:esp32:m5stack_paperdiy:PSRAM=opi EpubReader
```

Copy `.epub` files to the root of a FAT32 microSD card.

## Controls

| Button | Action |
| --- | --- |
| A click | DOWN: next book / chapter / page |
| A hold | UP: previous book / chapter / page |
| B click | SELECT: open book or chapter, or go back to the list |

## Libraries

Install from the Library Manager: **M5Unified** and **M5GFX**.
SD, SPI and WiFi come with the ESP32 core.

These are bundled in `src/` because they are not in the Library Manager:

| Folder | What | Source |
| --- | --- | --- |
| `src/Epub` | ePub, zip, HTML parsing and layout | original project's `lib/Epub`, lightly patched |
| `src/miniz` | zip / inflate | original project's `lib/miniz-2.2.0`; `tdefl_*` renamed `epub_tdefl_*` to avoid a link clash with M5GFX's bundled miniz |
| `src/tinyxml2` | XML parser | [leethomason/tinyxml2](https://github.com/leethomason/tinyxml2) v10.0.0 (`tinyxml2.cpp`, `tinyxml2.h`) |
| `src/M5GfxRenderer.h` | draws via M5GFX | new, replaces the epdiy renderers |

## Differences from the original

- FreeSerif 12pt fonts from M5GFX instead of generated epdiy fonts (Latin only).
- PNG/JPEG are decoded by M5GFX.
- Deep sleep, touch controls and SPIFFS are not ported.
- EPUB3 books are supported: the table of contents is read from the `nav` document when there is no NCX.
