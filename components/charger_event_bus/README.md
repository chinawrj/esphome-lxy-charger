# Charger event bus

`event_core.h` contains the platform-independent contract and queue. `charger_event_bus.h/.cpp` adapts it to ESPHome; `__init__.py` exposes `charger_event_bus:` and optional `on_event:` automations.

All modules communicate through `Event`. The BLE module owns transport, protocol validation and transaction results. Optional web, LCD, button and LED modules must not include or call the BLE component.

- `request(type, source, voltage, current)` accepts the four `REQUEST_*` types, assigns a nonzero monotonically increasing ID and queues an event. It returns `0` if invalid, full or IDs are exhausted. Apply payload validation belongs to BLE.
- `publish(event)` copies the event into a 32-entry FIFO and returns `false` on overflow. Callers must handle failure; a rejected event is not delivered and does not update state.
- `subscribe(callback)` registers one of up to 16 observers before dispatch starts. Empty callbacks, overflow and registration during a callback are rejected.
- `snapshot()` is read-only. Only dispatched response events update it; requests do not optimistically mark the charger connected or busy.
- The wrapper delivers at most 8 events per loop. Callback-produced events join the FIFO. Nested dispatch is ignored, so callback chains cannot recurse or occupy a loop indefinitely.

Publish and request never invoke a module synchronously. This core is for ESPHome's main loop; producers from another task must first defer to the main loop. An event reference is valid only during its callback; copy it if retaining it.

`CONNECTION` controls ready/connected/busy; not-ready invalidates cached voltage/current. `CONFIG` supplies setpoints only when connected and never independently asserts readiness. BLE must therefore publish connected before its first configuration, then ready after confirmation. `STATUS` represents BLE transaction outcomes and retains request correlation. Local UI feedback uses `INPUT`, which cannot change charger state. `NETWORK_STATE` supplies or clears an address independently of BLE.

Native tests live outside this component so their `main()` cannot enter firmware builds:

```sh
python3 tests/test_event_core.py
```

They cover all 16 LCD/Button/LED/Web combinations using independent module observers/producers, plus queue overflow, FIFO order, bounded/reentrant dispatch, request types and IDs, correlation, stale/offline readings and reconnect without replay. They do not emulate BLE packets or claim hardware verification. On a Mac with mismatched Command Line Tools, pass `--sdk` with the matching Xcode macOS SDK path.
