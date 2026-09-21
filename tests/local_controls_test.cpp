#include "esphome/components/charger_buttons/charger_buttons.h"
#include "esphome/components/charger_indicator/charger_indicator.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace esphome;
using namespace charger_event_bus;

struct Pure {
  charger_buttons::ButtonsController controller;
  Snapshot state;
  std::vector<Event> requests;
  uint32_t now{0}, sequence{0};
  bool display_enabled{true};
  bool accept{true};
  Pure() {
    state.connected = state.ready = true;
    state.voltage = 58.4f; state.current = 5.1f;
    controller.set_requester([this](EventType type, float v, float a) {
      if (!accept) return uint32_t{0};
      Event event{}; event.type = type; event.voltage = v; event.current = a;
      requests.push_back(event);
      return static_cast<uint32_t>(requests.size());
    });
    heartbeat();
  }
  void heartbeat(bool ready = true) {
    state.ui_display_ready = ready;
    Event event{}; event.type = EventType::UI_DISPLAY; event.ui_display_ready = ready;
    controller.observe(event, state, now);
  }
  void edge(bool a, bool down) { controller.edge(a, down, ++sequence, now, state); }
  void press(bool a, bool long_press = false) {
    if (display_enabled) heartbeat();
    edge(a, true); now += long_press ? 800 : 100;
    if (display_enabled) heartbeat();
    edge(a, false); now += 60;
  }
  UiMode mode() const { return controller.ui_event().ui_mode; }
  void confirmation() {
    press(true, true); assert(mode() == UiMode::EDIT);
    press(true); assert(controller.ui_event().voltage == 58.3f);
    press(true, true); assert(mode() == UiMode::CONFIRM);
    assert(requests.empty());
  }
};

struct Wrapper {
  ChargerEventBus bus;
  charger_buttons::ChargerButtons buttons;
  charger_indicator::ChargerIndicator indicator;
  output::BinaryOutput led;
  std::vector<Event> requests;
  uint32_t sequence{0};
  explicit Wrapper(bool with_buttons = true) {
    fake_millis = 0;
    assert(bus.subscribe([this](const Event &event) {
      if (EventCore::is_request(event.type)) requests.push_back(event);
    }));
    if (with_buttons) { buttons.set_event_bus(&bus); buttons.setup(); }
    indicator.set_event_bus(&bus); indicator.set_output(&led); indicator.setup();
    drain();
  }
  void drain() { for (int i = 0; i < 12; ++i) bus.loop(); }
  void emit(Event event) { assert(bus.publish(event)); drain(); }
  void heartbeat(bool ready = true) {
    Event event{}; event.type = EventType::UI_DISPLAY; event.ui_display_ready = ready; emit(event);
  }
  void ready() {
    Event event{}; event.type = EventType::CONNECTION; event.connected = event.ready = true; emit(event);
    event = Event{}; event.type = EventType::CONFIG; event.voltage = 58.4f; event.current = 5.1f; emit(event);
  }
  void edge(bool a, bool down) {
    Event event{}; event.type = EventType::INPUT; event.source = "buttons_gpio";
    event.request_id = ++sequence;
    event.message = a ? (down ? "a_down" : "a_up") : (down ? "b_down" : "b_up");
    emit(event); buttons.loop(); drain();
  }
  void press(bool a, bool long_press = false, bool display = true) {
    if (display) heartbeat();
    edge(a, true); fake_millis += long_press ? 800 : 100;
    if (display) heartbeat();
    edge(a, false); fake_millis += 60;
  }
};

int main() {
  { // Independent holds: entering/reviewing a draft performs no BLE operation.
    Pure f; f.confirmation();
    f.press(true, true);
    assert(f.mode() == UiMode::SUBMITTING && f.requests.size() == 1);
    assert(f.requests[0].type == EventType::REQUEST_APPLY_CONFIG);
    assert(f.requests[0].voltage == 58.3f && f.requests[0].current == 5.1f);
    f.press(true, true); f.press(false); assert(f.requests.size() == 1);
    Event terminal{}; terminal.type = EventType::STATUS; terminal.request_id = 1; terminal.result = Result::VERIFIED;
    f.controller.observe(terminal, f.state, f.now);
    assert(f.mode() == UiMode::VIEW);
  }
  { // An unchanged edit is not a command, and current editing freezes voltage.
    Pure f; f.press(true, true); f.press(true, true);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.press(true); f.press(true, true);
    assert(f.controller.ui_event().ui_field == UiField::CURRENT);
    for (int i = 0; i < 9; ++i) f.press(true);
    assert(f.controller.ui_event().current == 4.9f);
    for (int i = 0; i < 9; ++i) f.press(false);
    assert(f.controller.ui_event().current == 5.1f && f.controller.ui_event().voltage == 58.4f);
    assert(f.requests.empty());
  }
  { // No LCD package means read-only operation, even after repeated long presses.
    Pure f; f.display_enabled = false; f.heartbeat(false);
    for (int i = 0; i < 5; ++i) f.press(true, true);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.press(false);
    assert(f.requests.size() == 1 && f.requests[0].type == EventType::REQUEST_READ_CONFIG);
  }
  for (int reason = 0; reason < 6; ++reason) {
    Pure f; f.confirmation();
    if (reason == 0) f.heartbeat(false);
    if (reason == 1) { f.display_enabled = false; f.now += 3000; }
    if (reason == 2) f.state.ready = false;
    if (reason == 3) f.state.busy = true;
    if (reason == 4) f.state.current = 5.0f;
    if (reason == 5) { f.now += 30000; f.heartbeat(); }
    f.controller.tick(f.now, f.state);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // A overlapping B blocks both releases and discards an edit.
    Pure f; f.confirmation();
    f.edge(true, true); f.now += 400; f.edge(false, true); f.now += 500;
    f.edge(true, false); f.edge(false, false);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // An omitted edge cannot join presses into a confirmation hold.
    Pure f; f.confirmation(); f.edge(true, true); f.now += 800;
    ++f.sequence; f.edge(true, false);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // uint32 time/sequence wrap and publication rejection remain safe.
    Pure f; f.sequence = 0xfffffffeu; f.now = 0xffffff00u; f.heartbeat();
    f.press(true, true); assert(f.mode() == UiMode::EDIT);
    f.press(true); f.press(true, true); f.accept = false; f.press(true, true);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // An invisible confirmation can no longer authorize Apply.
    Pure f; f.confirmation(); f.controller.display_publish_failed();
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // Actual wrapper: event routing, UI snapshot, one frozen request and terminal ID.
    Wrapper f; f.ready(); f.heartbeat();
    assert(f.bus.snapshot().ui_buttons_ready);
    f.press(true, true); f.press(true); f.press(true, true);
    assert(f.bus.snapshot().ui_mode == UiMode::CONFIRM && f.requests.empty());
    f.press(true, true);
    assert(f.requests.size() == 1 && f.requests[0].source == "buttons");
    assert(f.requests[0].voltage == 58.3f && f.requests[0].current == 5.1f);
    Event result{}; result.type = EventType::STATUS; result.request_id = f.requests[0].request_id + 1;
    result.result = Result::FAILED; f.emit(result); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_mode == UiMode::SUBMITTING);
    result.request_id = f.requests[0].request_id; result.result = Result::VERIFIED;
    f.emit(result); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_mode == UiMode::VIEW && f.bus.snapshot().ui_result == Result::VERIFIED);
  }
  { // Actual wrapper without a display never issues settings requests.
    Wrapper f; f.ready();
    for (int i = 0; i < 4; ++i) f.press(true, true, false);
    assert(f.requests.empty());
    f.press(false, false, false);
    assert(f.requests.size() == 1 && f.requests[0].type == EventType::REQUEST_READ_CONFIG);
  }
  { // Refresh at non-default settings is distinct from applying an old draft.
    Wrapper f; f.ready(); f.heartbeat();
    f.press(true, true); f.press(true); f.press(false, true); // Cancel a changed draft.
    Event config{}; config.type = EventType::CONFIG;
    config.voltage = 58.2f; config.current = 4.9f; f.emit(config);
    f.press(false);
    assert(f.bus.snapshot().ui_mode == UiMode::REFRESHING);
    assert(f.bus.snapshot().voltage == 58.2f && f.bus.snapshot().current == 4.9f);
    assert(f.requests.size() == 1 && f.requests[0].type == EventType::REQUEST_READ_CONFIG);
    f.press(true, true); f.press(false);
    assert(f.requests.size() == 1);
    Event result{}; result.type = EventType::STATUS;
    result.request_id = f.requests[0].request_id; result.result = Result::FAILED;
    f.emit(result); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_mode == UiMode::VIEW);
  }
  { // Failed UI event publication cancels before any settings request is submitted.
    Wrapper f; f.ready(); f.heartbeat(); f.press(true, true); f.press(true);
    // Deliver only the press/release input first, without the wrapper's loop.
    auto input = [&](bool down) {
      Event event{}; event.type = EventType::INPUT; event.source = "buttons_gpio";
      event.message = down ? "a_down" : "a_up"; event.request_id = ++f.sequence;
      f.emit(event);
    };
    input(true); fake_millis += 800; f.heartbeat(); input(false);
    Event filler{}; while (f.bus.publish(filler)) {}
    f.buttons.loop(); f.drain(); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_mode == UiMode::VIEW && f.requests.empty());
  }
  { // LED controller pulse widths, priority, expiry, and wrap.
    charger_indicator::IndicatorController led;
    Snapshot state;
    assert(led.level(state, 0) && !led.level(state, 80) && led.level(state, 3000));
    state.connected = true;
    assert(led.level(state, 400) && !led.level(state, 600));
    state.ready = true; assert(led.level(state, 600));
    state.busy = true; assert(led.level(state, 1000) && !led.level(state, 1125));
    state.busy = false;
    Event event{}; event.type = EventType::STATUS; event.result = Result::VERIFIED; led.observe(event, 1000);
    assert(led.level(state, 1000) && !led.level(state, 1100) && led.level(state, 1200));
    assert(!led.level(state, 1300) && led.level(state, 4000));
    event.result = Result::REJECTED; led.observe(event, 5000);
    assert(led.level(state, 5299) && !led.level(state, 5300) && led.level(state, 5500));
    event.result = Result::UNKNOWN; led.observe(event, 6000);
    assert(led.level(state, 6000) && led.level(state, 6250) && led.level(state, 6500));
    assert(!led.level(state, 6600));
    event.result = Result::VERIFIED; led.observe(event, 6700);
    assert(!led.level(state, 6700) && led.level(state, 12000));
    event.result = Result::FAILED; led.observe(event, 0xffffff00u);
    assert(led.level(state, 0xffffff00u) && !led.level(state, uint32_t(0xffffff00u + 600)));
  }
  { // LED wrapper works without a buttons component and never submits a request.
    Wrapper f(false); f.indicator.loop(); assert(f.led.state);
    fake_millis = 100; f.indicator.loop(); assert(!f.led.state);
    f.ready(); f.indicator.loop(); assert(f.led.state);
    const unsigned writes = f.led.writes;
    f.indicator.loop(); assert(f.led.writes == writes && f.requests.empty());
  }
  puts("PASS: pure and actual button/LED wrappers, explicit single Apply, bounds, no-LCD read-only, heartbeat, cancellation, chord/lost-edge/wrap, UI overflow and nonblocking patterns");
}
