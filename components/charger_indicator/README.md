# Optional BLE connection LED

The original M5StickC Plus LED is physically red; it is not an RGB status light.
The component reads only the event-bus connection snapshot and drives GPIO10
through an inverted binary output. It works without buttons, LCD or web.

| LED | Meaning |
| --- | --- |
| Off | BLE is disabled and no actual link remains |
| 500 ms on / 500 ms off | BLE connection is enabled; searching or reconnecting |
| Steady red | An actual BLE link is connected |

`connected` takes priority over `connection_enabled`. The light stays steady
during GATT initialization, configuration reads, setting transactions and missing
or stale telemetry. Errors and Apply results do not change its pattern. The LED
does not indicate charger output enablement, load, charging or measured current.
Readiness, measurements and action results are separate information on the LCD.

Timing is nonblocking. The wrapper writes the binary output only when its level
changes. The portable controller and real wrapper are covered by
`python3 tests/test_local_controls.py` (add `--sdk PATH` on macOS if required).
