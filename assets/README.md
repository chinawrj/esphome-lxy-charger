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


## Chinese LCD labels

`ChargerSansSC.ttf` is a renamed, regular-weight subset of Google's [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc), distributed with [NotoSansSC-OFL.txt](NotoSansSC-OFL.txt). The original font has the reserved name “Source”; the modified subset uses “Charger UI SC”. It contains ASCII and the Chinese/punctuation characters used by the shared display view, listed in `ui-glyphs.yaml`.

Firmware builds use the local subset and do not fetch fonts from the network. To regenerate after adding labels, download the official `NotoSansSC[wght].ttf`, install `fonttools==4.60.1`, and run `python tools/subset_ui_font.py --source /path/to/NotoSansSC.ttf`. The source is instantiated at weight 400. Run `python tests/render_lcd.py` to verify glyph inventory and the bounds of each documented screen. The original font's copyright/license notices are retained; the original full font is not required in the public repository.
