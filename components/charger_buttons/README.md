# Optional two-button editor

`charger_buttons` owns only local input/edit state. It reads event-bus snapshots,
publishes `UI_STATE`, and submits explicit read or Apply requests through the bus.
It never calls the BLE, LCD or web modules. The web adapter retains its own drafts.
Successful setup announces `UI_CONTROLS.ui_buttons_ready=true`; initialization
failure announces false, so an LCD-only configuration does not show button hints.

| Screen | A short | B short | A long | B long |
| --- | --- | --- | --- | --- |
| View | Select voltage/current | Refresh / connect / wait | Enter selected editor | Help |
| Edit | Subtract 0.1 | Add 0.1 | Review confirmation | Cancel |
| Confirm | No action | Cancel | Submit Apply once | Cancel |
| Waiting | No action | No action | No action | No action |
| Help | Return only | Return only | No action | No action |

On View, short B refreshes a ready, idle charger. If BLE is both disconnected
and disabled, it requests connection once. If automatic connection is already
enabled or GATT initialization is in progress, it displays a waiting notice
without requesting another connection. `CONNECTING` tracks its own request ID
and reports `CONNECTED` or `CONNECT_FAILED`, separately from refresh and Apply.
Connection and refresh remain available without an LCD.

Refresh uses the distinct `REFRESHING` UI mode: the LCD keeps showing current
readback rather than old editing drafts or an Apply confirmation. Both refresh
and Apply wait for a terminal result with their own bus-assigned request ID.

A gesture is evaluated once, on release. Short presses are 40–799 ms after GPIO
debouncing; long presses are 800–5000 ms. Holding does not repeat adjustments.
The two long presses for review and submission must be separate press/release
cycles. A simultaneous two-button gesture is suppressed and cancels an edit.
At 800 ms the `UiHold` hint changes to “release to edit/review/apply/cancel/help”;
reaching the threshold never executes an action. A hold longer than five seconds
shows “release and try again” and its release performs no action. Losing a mode
while holding invalidates that entire gesture. Help requires a live LCD, exits
after 30 seconds without an action, and consumes the return key without also
refreshing, connecting or editing.

Entering edit freezes both values from the same current readback. Only the
selected value changes; bounds are 50.0–93.0 V and 1.0–10.0 A. Unchanged drafts
are discarded without a write. Drafts expire after 30 seconds without an action.
Loss of readiness, another operation becoming busy, a changed readback baseline,
or loss of display availability cancels an unsubmitted draft. Submitted requests
are never automatically replayed, even after reconnect.

The LCD must publish `UI_DISPLAY` with `ui_display_ready=true` after a real draw,
at least every three seconds. A missing/expired heartbeat or a false capability
blocks edit and Apply. **Without the LCD package, buttons only select and request
connection/readback; they cannot submit settings or open invisible Help.** Failure to publish an edit/confirmation
`UI_STATE` also cancels the draft.

UI feedback uses typed `UiNotice` values, not parsing diagnostic strings.
`APPLIED` and `REFRESHED` are emitted only after a terminal STATUS for this
controller's own pending request ID. Failure/unknown and cancellation/limit/wait
notices have distinct types. UI changes set `UI_STATE.sampled_at`, reduced to
`Snapshot.ui_updated_at`; background traffic and LCD heartbeats do not renew
this timestamp or overwrite the local notice. The display owns notice expiry.

GPIO integration sends `INPUT` from `buttons_gpio`, with messages `a_down`,
`a_up`, `b_down`, `b_up`. For these INPUT events only, `request_id` is a shared
uint32 edge sequence, incremented even if publication fails; wrap is supported.
A missing edge cancels the gesture, preventing a missed release from becoming
an accidental long press. Real command request IDs still come from the bus.

The portable controller and the actual ESPHome wrapper are covered by
`python3 tests/test_local_controls.py` (add `--sdk PATH` on macOS if required).

## Idle meter and wake gesture

After 15 seconds without input edges in VIEW, the controller publishes
`UI_STATE.ui_mode=METER` if the LCD heartbeat is available and no operation or
button hold is active. LCD heartbeats and BLE telemetry do not count as button
activity. A press wakes VIEW immediately; every edge of that hold/chord is
consumed through release, including long presses. The next separate press uses
normal home controls. Other pages and pending requests reset the home idle
interval; time comparisons support uint32 rollover. No LCD means no meter;
without this Button module the LCD remains on VIEW.

## Five-minute backlight idle

An independent 300000 ms timer spans Home and Meter. Only button activity or an active local interaction/transaction restarts it; telemetry and LCD heartbeats do not. `UI_STATE.ui_backlight_on` carries the request through the bus. Any button-down wakes Home and consumes the full gesture, including long holds and chords. Missing input edges do not become commands. No LCD heartbeat keeps the request on; the LCD adapter keeps it on when Button is absent/unavailable.
