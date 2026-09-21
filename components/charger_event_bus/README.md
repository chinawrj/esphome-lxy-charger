# Charger event bus

`event_core.h` is the platform-independent typed contract, FIFO and state
reducer. `charger_event_bus.h/.cpp` adapts it to ESPHome; `__init__.py` exposes
`charger_event_bus:` and optional `on_event:` automations. Application modules
communicate through events and snapshots, without calling another module's
BLE, UI or entity objects.

## Queue and requests

- `request(type, source, voltage, current)` accepts the four `REQUEST_*` types,
  assigns a nonzero increasing ID, and queues the request. It returns `0` for an
  invalid type, full queue or exhausted IDs. BLE validates Apply values.
- `publish(event)` copies into a 32-entry FIFO and returns false on overflow.
  Rejected events are neither delivered nor reduced into the snapshot.
- `subscribe(callback)` registers up to 16 observers before dispatch. Empty
  callbacks and registration during a callback are rejected.
- The wrapper dispatches at most 8 events per loop. The reducer runs before
  observers; callback-produced events join the FIFO. Nested dispatch is ignored.
- `snapshot()` is read-only. Requests do not optimistically update actual link,
  configuration, readiness or transaction state.

Publish and request never invoke a module synchronously. The core is for the
main application loop, without cross-thread locking. Producers in other tasks
must first defer there. Callback event references are temporary; copy any event
that must outlive its callback. IDs correlate local requests and results; they
are not additional fields in the charger's BLE protocol.

## Connection, configuration and operations

`CONNECTION` separately carries:

| Field | Meaning |
| --- | --- |
| `connection_enabled` | The local BLE client is enabled to connect/reconnect |
| `connected` | An actual BLE link exists |
| `ready` | Notification transport and fresh configuration readback are ready |
| `busy` | A foreground request is being processed |

The reducer requires `connected` for ready/busy. Not-ready invalidates cached
setpoints; disconnect also invalidates measurements. Enabled is not equivalent
to connected: an enabled disconnected device may still be searching. STATUS does
not independently change the connection fields in the snapshot.

`CONFIG.voltage/current` are setpoints, accepted only while connected. CONFIG
never asserts readiness. BLE therefore reports connection before configuration,
and readiness after confirmation. `STATUS` updates operation result, busy and
`last_request_id`. Results are `INFO`, `ACCEPTED`, `VERIFIED`, `REJECTED`, `FAILED`
and `UNKNOWN`; Apply is VERIFIED only after matching echo plus fresh readback.
A queued command, HTTP response or echo alone is not verified device state.

`RAW_STATUS` retains undecoded protocol bytes and their receive time. It is not
measurement data. `NETWORK_STATE` updates an address separately from BLE; its
`connected` field denotes network availability only. INPUT and requests do not
directly change charger state.

## Measurement capability and availability

`TELEMETRY_CAPABILITY.telemetry_supported` is an explicit decoder capability,
not a claim inferred from raw packet arrival. A false declaration clears valid
measurements and `telemetry_seen`. Declaring support does not itself produce a
sample or refresh its age.

`TELEMETRY` carries separate `output_voltage/output_current`, `sampled_at` and
`telemetry_valid`. The reducer accepts a valid measurement only when connected,
capability is enabled, the event is marked valid, and every declared channel is finite.
`telemetry_channels` is a bitmask (1=voltage, 2=current; default 3); undeclared
channels are always NaN. Changing channels or `telemetry_inferred` invalidates old samples.
`telemetry_inferred` marks a provisional mapping, independently of freshness.
Unsupported/offline samples cannot bypass the gate. Invalid samples become NaN;
disconnect clears measurements so reconnect cannot revive them.

**The current BLE service advertises voltage only (`channels=1`), with an
inferred mapping.** It decodes status DATA[3:5] as big-endian tenths of a volt,
based on restart captures, without app/meter calibration. Current remains NaN.
Raw status and configuration events alone never create a voltage sample.
An unloaded charger must not be assumed to measure zero current.

Consumers call `snapshot.output_state(now)` or `telemetry_fresh(now)`. Freshness
requires connected, supported, valid data younger than 6000 ms. Time is
boot-relative milliseconds, using unsigned subtraction across counter wrap.
`OutputState` describes data availability, not the charger's output switch.
The evaluation order is:

| State | Condition |
| --- | --- |
| `DISCONNECTED` | No actual BLE link |
| `LIVE` | A fresh valid measurement is available |
| `INITIALIZING` | No live measurement and protocol not ready |
| `UNSUPPORTED` | Ready, but decoding capability is false |
| `WAITING` | Supported, but no sample received on this connection |
| `STALE` | The last valid sample has expired |
| `INVALID` | A sample was received but is invalid/nonfinite |

The fixed red LED uses only actual connection: connected is steady on;
otherwise enabled is 500 ms on/off; otherwise off. Readiness, telemetry and
operation results never override it.

## Optional local UI

`UI_DISPLAY` reports software display availability from each draw callback.
Buttons track reception time and require a heartbeat younger than 3 seconds.
`UI_CONTROLS.ui_buttons_ready` declares installed controls so an LCD-only build
can omit button selection/highlights and instructions.

`UI_STATE` has its own `ui_mode`, `ui_notice`, `ui_hold`, selected field, frozen
voltage/current draft and request ID. Its `sampled_at` becomes
`Snapshot.ui_updated_at`. It cannot change BLE state, setpoints or transaction
correlation. Modes distinguish View, Edit, Confirm, Submitting, Refreshing,
Connecting and Help. `UiNotice` distinguishes applied settings, refreshed
parameters, connection success/failure and local cancellation/validation errors.
Only the controller's own pending request ID completes its operation.

`UiHold` is a release hint, never an action: 800 ms updates the hint; release
within 800–5000 ms performs the intended long-press action. A hold over 5 seconds
shows RELEASE and does nothing on release. Local UI changes update feedback age;
background status reports and display heartbeats alone do not. The current
normal-page renderer shows ordinary feedback for 3 seconds and FAILED, UNKNOWN
or CONNECT_FAILED for 6 seconds; the snapshot retains the notice after display
expiry. Busy/connecting/refreshing messages may take display priority.

On View, short B refreshes when ready/idle, requests connection once when
both disconnected and disabled, or reports waiting while connection is already
enabled/initializing. Long B opens Help only with a live LCD. A/B short press in
Help returns without also performing its normal action. Help expires after
30 seconds of inactivity. Without LCD, connection/readback still work, but
editing, Apply and invisible Help are blocked.

For physical `INPUT` from `buttons_gpio` only, `request_id` is the shared uint32
GPIO edge sequence. Missing edges cancel the gesture; this sequence is distinct
from IDs allocated for `REQUEST_*` and from `UI_STATE` request correlation.

## Tests

Native test entry points (outside components so `main()` cannot enter firmware):

```sh
python3 tests/test_event_core.py
python3 tests/test_ble_serialization.py
python3 tests/test_local_controls.py
python3 tests/test_telemetry_view.py
python3 tests/test_web_telemetry.py
```

These cover queue/routing boundaries, the real BLE state machine with fake GATT,
real optional-module wrappers, capability/freshness reduction and display/web
consumers. They do not verify radio, panel wiring or physical keys. On macOS with
mismatched Command Line Tools, pass `--sdk` with a matching Xcode SDK path.


## Board battery

`BOARD_BATTERY` is independent of BLE output telemetry. The producer supplies
validity, battery presence, voltage and separate charge/discharge mA readings.
The snapshot stores a dedicated RX timestamp and signed `battery_current_ma`
(charge minus discharge). Nonfinite/negative channel values invalidate a present
battery sample. Absent battery and failed sampling remain distinguishable;
`battery_fresh(now)` expires after 6 seconds and does not depend on BLE connection.
This event cannot change BLE setpoints, link/readiness/busy or transaction IDs.
