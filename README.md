# LXY BLE Charger · ESPHome

**English** | [简体中文](README.zh-CN.md)

A BLE charger controller built with **ESPHome 2026.9.0 and ESP-IDF**. BLE and an internal event bus form the required core. The LCD, physical buttons, LED, and standalone web interface are four independent optional modules. The web interface works without a Home Assistant server; a build without Web can still connect to the charger and read its configuration.

The supported protocol uses service `FFF0`, writes to `FFF2`, and notifications from `FFF1`. A matching brand or advertised device name does not establish protocol compatibility. The main LCD view is reserved for **output voltage and current**; charging **setpoints** are shown separately in smaller text. Missing or stale output data is shown as `--.-`, never substituted with setpoints. **Live output decoding is not enabled yet**: the byte mapping and scaling still need validation against nonzero charger samples. Temperature decoding and a charging-output switch are not implemented.

## Screenshots

### LCD layout previews

These previews are generated from the **same C++ view model used by the firmware**. They are not hardware photographs. [The renderer](tests/render_lcd.py) uses Pillow for fonts, so individual pixels can differ from the device.

<p>
  <img src="docs/images/lcd-output-preview.png" width="320" alt="LCD output view with unavailable live measurements shown as dashes">
  <img src="docs/images/lcd-edit-preview.png" width="320" alt="LCD editing preview with a sample 58.3 V and 5.1 A draft">
  <img src="docs/images/lcd-confirm-preview.png" width="320" alt="LCD confirmation preview requiring another A hold before Apply">
</p>

Left to right: output view, edit, confirmation. The output view deliberately has no valid live sample and shows `--.-`; its smaller setpoints are example UI data. Editing and confirmation use a **58.3 V / 5.1 A sample draft**, not a captured output measurement.

### Web interface on the device

<img src="docs/images/web-ui.png" width="702" alt="Actual ESPHome web interface showing 58.4 V and 5.1 A readback setpoints, unavailable output measurements, and live output validity off">

Captured from a M5StickC Plus running the v2.1 development firmware after OTA. A read-only local proxy preserved the device's original page and live event data; readings were not simulated or replaced. The image is cropped to omit IP and diagnostic details. Readback is **58.4 V / 5.1 A**; output fields are **NA** and `Live output valid` is **OFF**, consistent with the pending output decoder.

## Choose a configuration

| Entry file | Hardware | Included modules |
|---|---|---|
| [esp32-minimal.yaml](esp32-minimal.yaml) | Classic ESP32 with BLE, 4 MB flash | BLE + event bus |
| [esp32-headless.yaml](esp32-headless.yaml) | Classic ESP32 with BLE, 4 MB flash | Core + Web |
| [atoms3u.yaml](atoms3u.yaml) | M5Stack ATOMS3U, ESP32-S3, 8 MB flash | Core + Web |
| [m5stickc-plus.yaml](m5stickc-plus.yaml) | Original M5StickC Plus / v1.1, ESP32-PICO-D4 and AXP192 | Core + LCD + Button + LED + Web |

The M5StickC Plus hardware packages do not support Plus2. ATOMS3U has no LCD package in this project, and the M5StickC pin assignments should not be applied to a generic ESP32 board. ESP32-S2 has no BLE and is not supported. See the official [M5StickC Plus](https://docs.m5stack.com/en/core/m5stickc_plus) and [ATOMS3U](https://docs.m5stack.com/en/core/AtomS3U) hardware documentation.

The M5StickC Plus 1.1 LCD, LED, and serial installation flow also draw on the same author's [m5stickplus1.1 project](https://github.com/chinawrj/m5stickplus1.1), adapted here to independent ESPHome packages.

Choose modules in the entry file's `packages` section. Always keep `common`:

```yaml
packages:
  common: !include packages/common.yaml
  web: !include packages/web.yaml
  lcd: !include packages/m5stickc-plus-display.yaml
  buttons: !include packages/m5stickc-plus-buttons.yaml
  led: !include packages/m5stickc-plus-led.yaml
```

Remove an optional package line to omit its module and resources. The three M5StickC hardware packages require that board. LCD, Button, LED, and Web communicate through the event bus; none calls another optional module directly. See the [architecture and event contract](docs/architecture.md) (Chinese).

## Build and install

You need Python, C++ build tools, and an internet connection for the first ESPHome/ESP-IDF dependency download. From the project directory, on macOS or Linux:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp secrets.example.yaml secrets.yaml
```

Edit `secrets.yaml` and set your charger's `charger_mac`. The BLE configuration reads it through `mac_address: !secret charger_mac`. When enabling Web, also fill in the example's Wi-Fi, fallback access point, web login, and OTA credentials. `secrets.yaml` and generated firmware are excluded from version control; compiled firmware can contain those credentials.

Validate, compile, and upload the entry file for your board, for example:

```sh
esphome config esp32-headless.yaml
esphome compile esp32-headless.yaml
esphome upload esp32-headless.yaml --device /dev/cu.YOUR_PORT --upload_speed 115200
esphome logs esp32-headless.yaml --device /dev/cu.YOUR_PORT
```

Replace the YAML filename and serial port as needed. The first upload replaces the board's previous firmware; ESPHome's `upload` command selects the appropriate image and addresses. A core-only build has no Wi-Fi or OTA, so use USB for uploads and logs. See the [ESPHome CLI documentation](https://esphome.io/guides/cli/).

Disconnect the phone app from the charger before connecting the controller. At startup, the controller discovers GATT characteristics, subscribes to notifications, and reads the setpoints. These steps do not send a settings command.

## Use the optional modules

### Web

The controller first tries the Wi-Fi networks configured in `secrets.yaml`. Open its local IP address in a browser. If it cannot connect, join its fallback access point and open `http://192.168.4.1/`, then sign in with your web credentials. The Wi-Fi provisioning page is `/wifi`.

Page assets are stored on the device and do not require an external CDN. Home Assistant's native API is not enabled. Omitting Web also omits this page and its Wi-Fi access point.

| Control or reading | Meaning |
|---|---|
| `Charger ready` | GATT is ready and this connection has returned its setpoints |
| `Output voltage/current` | Live output fields; currently unavailable because the BLE output decoder is not enabled |
| `Live output valid` | Whether a valid output sample is present and less than 6 seconds old |
| `Readback set voltage/current` | Setpoints read from the charger |
| `Target voltage/current` | Local drafts; editing does not send settings over BLE |
| `Apply settings` | Explicitly submit both draft values |
| `Refresh settings` | Read the configured setpoints |
| `BLE connection` | Manage the Bluetooth connection; does not switch charging output |
| `Command status` | Request acceptance, readback verification, rejection, or an uncertain result |

The allowed settings are **58.2–58.4 V and 4.9–5.1 A**, in **0.1** steps. These limits cover the protocol samples tested so far; they are not a statement of suitability for a particular battery.

### LCD

The landscape screen puts output voltage and current first, using large digits. Smaller `Set` values show the configured voltage and current. Output data expires after **6 seconds** without a valid update and is invalidated on disconnection; the screen then shows `--.-`. A configured voltage must not be read as a measured output voltage.

The compact screen also shows the selected setting, edit/confirmation state, and connection or transaction state. It omits the IP address, branding, temperature, and long status messages. A shared `charger_display` view model supplies the layout and editing state.

The current BLE service retains `84` as a raw frame and does not publish a validated output measurement, so the normal deployed view shows `--.-`. Output decoding still needs validation against nonzero charger samples. The UI's output fields and a layout preview do not establish measurement accuracy; see the revision-specific [verification record](docs/verification.md).

### Buttons

Button editing is available only when the LCD is included and usable. Hold for **800 ms to 5 seconds, then release** for a long press. Actions are evaluated on release; holds longer than 5 seconds are ignored as stuck input.

| Mode | Button A | Button B |
|---|---|---|
| View | Short: select voltage/current. Long: enter edit mode. | Short: refresh setpoints. |
| Edit | Short: decrease the selected draft by 0.1. Long: open confirmation. | Short: increase the selected draft by 0.1. Long: cancel. |
| Confirm | A second long press submits the paired voltage/current draft once. | Any press cancels. |

Entering Edit and opening Confirm do not send settings. Values remain within the captured limits above. Editing is cancelled after **30 seconds** without input, on disconnection, when another operation makes the charger busy, when the baseline configuration changes, or when the display becomes unavailable. A cancelled draft is not submitted or replayed later.

Without the LCD, the Button package provides selection and read-only refresh actions; it cannot enter editing or submit Apply. Buttons communicate through events and do not call the BLE service directly.

### LED

The red LED operates independently of the LCD and Web:

| State | Indication |
|---|---|
| Disconnected | 80 ms pulse every 3 seconds |
| Connected, protocol not ready | 500 ms on / 500 ms off |
| Ready and idle | Steady on |
| Transaction busy | 125 ms on / 125 ms off |
| `VERIFIED` | Two 100 ms pulses separated by 100 ms; repeat every 1.5 seconds for 3 seconds |
| `FAILED` / `UNKNOWN` | Three 100 ms pulses separated by 150 ms; repeat every 1.5 seconds for 6 seconds |
| `REJECTED` | Two 300 ms pulses separated by 200 ms; repeat every 1.5 seconds for 3 seconds |

These indications report connection and request results, not whether charging output is enabled. Priority is failure/unknown, then busy, then success/rejection notices, then the current connection state. A quick reconnect and successful read do not erase an active 6-second failure indication. Patterns are nonblocking.

## How settings are confirmed

Each Apply sends one `03` settings frame, waits for an `83` echo with the requested voltage and current, then sends a new `02/01` query. The returned `82` setpoints must match before the result becomes `VERIFIED`. `ACCEPTED` only means processing has begun. The `83` echo has no independent success code.

Background status polling uses the same serialized request channel: while `04` awaits `84`, one read or Apply request can wait with its own ID and frozen values. It is sent only after the status response. Background polling alone does not prevent local draft edits.

A timeout or disconnection clears the transaction and reports failure or an unknown outcome. Settings are never replayed automatically. Startup, reconnection, reads, and opening the web page do not Apply settings. Web drafts survive a BLE reconnect; after a controller reboot, the first valid readback initializes them.

Readback confirmation does not establish power-cycle persistence or actual electrical output. The output-enable/disable command has not been implemented. See the [captured protocol and its limits](docs/protocol.md) (Chinese).

## Tests and verification

| Check | Coverage | Limits |
|---|---|---|
| Native C++ tests | Captured frame decoding; event queues, IDs and invalidation; simulated event routing for all 16 module combinations; the real BLE transaction implementation with a fake clock and GATT transport | Does not validate radio behavior, GPIO wiring, or physical display appearance |
| ESPHome matrix | Required BLE + bus with all `2^4 = 16` LCD/Button/LED/Web combinations on M5StickC; a separate ATOMS3U build | Does not mean every combination was flashed to hardware |
| Recorded hardware checks | M5StickC Plus headless + Web and full-profile BLE reads, explicit Apply, and reconnect behavior; the released LCD display was accepted by the operator | ATOMS3U hardware and independent red-LED/button observations have not been verified |

The local-controls test compiles the real button and LED adapters and checks confirmation, cancellation, input loss, and indication timing. The telemetry view and Web tests use synthetic typed events to check freshness, invalidation, and presentation; they do not establish the charger's output byte mapping. The Web test compiles the real adapter for all eight combinations of its three optional telemetry entities.

The [verification record](docs/verification.md) identifies the tested revision and physical checks. The [16-combination report](docs/test-matrix.json) records build results. Check those records for the revision you are using; prior acceptance does not automatically verify a changed interface.

Run native tests:

```sh
python components/lxy_charger/test_protocol.py
python tests/test_event_core.py
python tests/test_ble_serialization.py
python tests/test_local_controls.py
python tests/test_telemetry_view.py
python tests/test_web_telemetry.py
```

Run the module matrix:

```sh
python tests/matrix.py --mode validate --cases all
python tests/matrix.py --mode generate --cases all
python tests/matrix.py --mode compile --cases all --jobs 2
```

Mask bits 0/1/2/3 select LCD/Button/LED/Web. For example, `--cases 0,15` selects the core-only and full builds. Results and logs default to `.esphome/matrix/`; change the location with `--work-dir`. The matrix uses example credentials and compiles without uploading to a device.

Configuration validation, code generation, and firmware compilation are separate checks. Native module substitutes verify event boundaries; they do not replace compilation of the real ESPHome adapters or hardware checks.

## Project layout

- `components/charger_event_bus/`: portable event queue and snapshot, plus the ESPHome wrapper.
- `components/lxy_charger/`: BLE service, GATT lifecycle, protocol decoder, and request confirmation.
- `components/charger_web/`: optional web entities, local drafts, and user requests.
- `components/charger_display/`: shared LCD layout and view model.
- `components/charger_buttons/`: local button editing, explicit confirmation, and cancellation.
- `components/charger_indicator/`: independent, nonblocking LED patterns.
- `packages/common.yaml`: required core.
- `packages/web.yaml`: optional web interface, Wi-Fi, and network events.
- `packages/m5stickc-plus-*.yaml`: independent LCD, button, and LED hardware packages.
- `assets/`: display font and its [OFL license information](assets/README.md).
- `tests/`: native checks and the configuration/build matrix.

Original captures, device backups, private credentials, and firmware containing credentials are excluded from the public source distribution.
