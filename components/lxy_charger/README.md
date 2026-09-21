# LXY pure BLE service

Targets ESPHome 2026.9.0 with ESP-IDF. This mandatory module owns only the captured charger protocol, GATT transport, and transaction state. It has no sensor, number, button, display, LED, Wi-Fi, Web, or Home Assistant entity dependency.

## YAML

```yaml
lxy_charger:
  id: charger
  ble_client_id: charger_ble
  event_bus_id: charger_bus
```

The parent BLE client owns the target address and connection lifecycle. The service discovers FFF0 / FFF2 / FFF1, registers through ESPHome's notification wrapper, and waits for the CCCD acknowledgment before the initial read-only configuration query. Only numeric handles are retained after ESPHome releases its GATT cache.

## Event boundary

All service requests and reports use charger_event_bus. UI modules must never call BLE service methods or access its state directly.

| Request event | Behavior |
|---|---|
| REQUEST_CONNECT | Enable the parent BLE client; read-only discovery/query follows |
| REQUEST_DISCONNECT | Disable BLE and invalidate observed readiness/readings |
| REQUEST_READ_CONFIG | Query the configured voltage/current |
| REQUEST_APPLY_CONFIG | Validate both values from the event payload and send exactly once |

Additional requests are rejected while a configuration query or Apply transaction is pending. An explicit Apply snapshots its event's voltage/current values. It requires a matching 83 echo before an independent 02/01 query; the resulting 82 configuration must match the snapshot to produce VERIFIED. CONFIG and STATUS replies retain the originating request_id. Startup/unsolicited data uses request_id 0.

Reports are CONNECTION, CONFIG, STATUS, and RAW_STATUS. CONFIG contains setpoints, not measured electrical output. STATUS carries structured INFO/ACCEPTED/VERIFIED/REJECTED/FAILED/UNKNOWN plus the current busy flag. RAW_STATUS is the complete checksum-verified 84 frame; unverified fields remain undecoded.

There are no automatic setting writes on boot or reconnect and no automatic setting replay. Any uncertain write, echo mismatch, timeout or link loss invalidates the transaction. A critical event queue overflow disables BLE and retries only the disconnected/unknown notice, never the charger command. Reconnection after that fault requires an explicit connection request.

## Verified range and wire protocol

C++ validates finite values, 0.1 increments and configured bounds: 50.0–93.0 V and 1.0–10.0 A. Voltage follows the operator-reported charger nameplate; current is explicitly widened to 1–10 A at the operator’s request despite a reported 3–10 A nameplate. Acceptance below 3 A remains unverified. Native fake-transport tests exercise all 431 voltage settings and 91 current settings, including matching echo/readback and rejection outside the bounds. Physical extended-range operation has not been tested. UI adapters expose the same bounds but cannot bypass BLE-side validation.

Frames remain 5E5E | L | CMD | DATA | XOR, with L = total length - 3 and XOR over L through the final data byte. Setter 03 carries 01 plus big-endian u16 voltage/current multiplied by 10. No output-enable/disable, binding, authentication or guessed calibration command is implemented.

## Validation

The unchanged charger_protocol.h and test_protocol.py cover captured commands, query/reply decoding, all notification split points, coalescing, invalid checksums/noise, and reset inside a decoder callback. These native tests passed before the event-boundary refactor, and neither protocol header nor test vectors changed during that refactor. They do not test radio or physical charging behavior.

Project-level records cover event routing tests, configuration combinations, full firmware compilation, and real-device validation separately. A CONFIG/VERIFIED response confirms the charger-reported configuration, not measured output or power-cycle persistence.

Official references: [BLE client](https://esphome.io/components/ble_client/) and [pinned BLEClientNode implementation](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/ble_client/ble_client.h).
