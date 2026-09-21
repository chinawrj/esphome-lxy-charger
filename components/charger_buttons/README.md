# Optional two-button editor

`charger_buttons` owns only local input/edit state. It reads event-bus snapshots,
publishes `UI_STATE`, and submits explicit read or Apply requests through the bus.
It never calls the BLE, LCD or web modules. The web adapter retains its own drafts.
Successful setup announces `UI_CONTROLS.ui_buttons_ready=true`; initialization
failure announces false, so an LCD-only configuration does not show button hints.

| Screen | A short | B short | A long | B long |
| --- | --- | --- | --- | --- |
| View | Select voltage/current | Read configuration | Enter selected editor | No action |
| Edit | Subtract 0.1 | Add 0.1 | Review confirmation | Cancel |
| Confirm | No action | Cancel | Submit Apply once | Cancel |
| Waiting | No action | No action | No action | No action |

Refresh uses the distinct `REFRESHING` UI mode: the LCD keeps showing current
readback rather than old editing drafts or an Apply confirmation. Both refresh
and Apply wait for a terminal result with their own bus-assigned request ID.

A gesture is evaluated once, on release. Short presses are 40–799 ms after GPIO
debouncing; long presses are 800–5000 ms. Holding does not repeat adjustments.
The two long presses for review and submission must be separate press/release
cycles. A simultaneous two-button gesture is suppressed and cancels an edit.

Entering edit freezes both values from the same current readback. Only the
selected value changes; bounds remain 58.2–58.4 V and 4.9–5.1 A. Unchanged drafts
are discarded without a write. Drafts expire after 30 seconds without an action.
Loss of readiness, another operation becoming busy, a changed readback baseline,
or loss of display availability cancels an unsubmitted draft. Submitted requests
are never automatically replayed, even after reconnect.

The LCD must publish `UI_DISPLAY` with `ui_display_ready=true` after a real draw,
at least every three seconds. A missing/expired heartbeat or a false capability
blocks edit and Apply. **Without the LCD package, buttons only select and request
readback; they cannot submit settings.** Failure to publish an edit/confirmation
`UI_STATE` also cancels the draft.

GPIO integration sends `INPUT` from `buttons_gpio`, with messages `a_down`,
`a_up`, `b_down`, `b_up`. For these INPUT events only, `request_id` is a shared
uint32 edge sequence, incremented even if publication fails; wrap is supported.
A missing edge cancels the gesture, preventing a missed release from becoming
an accidental long press. Real command request IDs still come from the bus.

The portable controller and the actual ESPHome wrapper are covered by
`python3 tests/test_local_controls.py` (add `--sdk PATH` on macOS if required).
