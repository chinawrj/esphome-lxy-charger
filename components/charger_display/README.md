# Optional LCD view

`charger_display.h` is a portable view model: it returns positioned labels using only an event-bus snapshot and the current boot-relative time. The LCD YAML owns power, SPI, the display and fonts, and draws these labels at 240 × 135.

The normal page gives measured output the largest type. Setpoints are separate and smaller. Missing, disconnected or ≥6-second-old measurements display `--.-`; setpoints never substitute for output. Button edit/confirmation pages show the immutable local drafts. A refresh keeps the output page and shows current readback values, not a cancelled draft.

The LCD publishes `UI_DISPLAY` after each draw. Its power initialization must have succeeded and the display must not have failed. Buttons require this heartbeat within 3 seconds. `UI_CONTROLS` lets the LCD omit button instructions when no Button module is installed. No BLE, Web, Button or LED object is referenced.

`tests/render_lcd.py` compiles this same view model and generates documentation previews with Pillow and the bundled Roboto font. They are layout previews, not hardware photos; font rasterization can differ by a pixel. `tests/test_telemetry_view.py` tests the real view and event reducer.
