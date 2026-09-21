# Optional status LED

This component reads only the internal event bus and drives a configured binary
output. It works independently of the buttons, LCD and web modules. GPIO10 on the
original M5StickC Plus is active-low; its package configures output inversion.
All timing uses the main loop and unsigned elapsed milliseconds, without delay.

| State/result | Pattern |
| --- | --- |
| Disconnected | 80 ms pulse every 3 seconds |
| Connected, awaiting ready | 500 ms on / 500 ms off |
| Ready | Steady on |
| Busy | 125 ms on / 125 ms off |
| VERIFIED | Two 100 ms pulses, 100 ms gap; repeat every 1.5 seconds for 3 seconds |
| REJECTED | Two 300 ms pulses, 200 ms gap; repeat every 1.5 seconds for 3 seconds |
| FAILED or UNKNOWN | Three 100 ms pulses, 150 ms gaps; repeat every 1.5 seconds for 6 seconds |

Fault indications take priority and remain visible through a quick reconnect.
Busy takes priority over success/rejection indications. After a notice expires,
the LED reflects the current connection state. The LED cannot report actual
charger output enablement, which is not a verified part of the BLE protocol.

The timing controller and actual output wrapper are covered by
`python3 tests/test_local_controls.py` (add `--sdk PATH` on macOS if required).
