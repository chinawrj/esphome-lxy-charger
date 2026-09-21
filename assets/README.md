# Display font

`Roboto.ttf` is the Roboto variable font distributed by Google Fonts. It is embedded into the firmware only for the ASCII glyphs configured in the display package.

- [Official font source](https://github.com/google/fonts/blob/main/ofl/roboto/Roboto%5Bwdth%2Cwght%5D.ttf)
- [Official license source](https://github.com/google/fonts/blob/main/ofl/roboto/OFL.txt)
- Local license: `Roboto-OFL.txt` (SIL Open Font License 1.1)

Downloaded on 2026-09-21.

The fonts belong only to the optional `m5stickc-plus-display.yaml` package. A
build without the LCD package does not include font resources in the firmware.
The display reads only the event bus snapshot and remains usable without button,
LED or web modules; without a network event it shows `Offline / USB`.
