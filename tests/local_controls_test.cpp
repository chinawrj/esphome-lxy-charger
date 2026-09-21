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
  for (bool upper : {false, true}) {
    Pure f; f.state.voltage = upper ? 92.9f : 50.1f;
    f.press(true, true); assert(f.mode() == UiMode::EDIT);
    f.press(!upper); assert(f.controller.ui_event().voltage == (upper ? 93.0f : 50.0f));
    f.press(!upper); assert(f.controller.ui_event().ui_notice == UiNotice::LIMIT);
    assert(f.controller.ui_event().voltage == (upper ? 93.0f : 50.0f) && f.requests.empty());
    f.press(true, true); assert(f.mode() == UiMode::CONFIRM && f.requests.empty());
    f.press(true, true); assert(f.requests.size() == 1);
    assert(f.requests[0].voltage == (upper ? 93.0f : 50.0f) && f.requests[0].current == 5.1f);
  }
  for (float value : {49.9f, 93.1f, 50.05f}) {
    Pure f; f.state.voltage = value; f.press(true, true);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }


  { // Only idle HOME enters the meter; display heartbeats/telemetry do not reset idle.
    Pure f;
    f.now = 14999; f.heartbeat(); assert(f.mode() == UiMode::VIEW);
    f.now = 15000; f.heartbeat(); assert(f.mode() == UiMode::METER);
    f.press(false, true);  // Wake with long B must not open Help or refresh.
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.now += 14999; f.heartbeat(); assert(f.mode() == UiMode::METER);
    f.press(true, true);  // Wake with long A must not edit.
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.press(true, true); assert(f.mode() == UiMode::EDIT);
    f.now += 16000; f.heartbeat(); assert(f.mode() == UiMode::EDIT);
  }
  { // Every edge resets idle; a held or simultaneous wake cannot become a command.
    Pure f; f.now = 15000; f.heartbeat();
    f.edge(true, true); f.edge(false, true); f.now += 1000; f.heartbeat();
    f.edge(true, false); f.edge(false, false);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.now += 14999; f.heartbeat(); assert(f.mode() == UiMode::VIEW);
    f.now += 1; f.heartbeat(); assert(f.mode() == UiMode::METER);
  }
  { // Idle timeout is safe across millis rollover and absent LCD.
    Pure f; f.now = UINT32_MAX - 1000; f.edge(true, true); f.edge(true, false);
    f.now += 14999; f.heartbeat(); assert(f.mode() == UiMode::VIEW);
    f.now += 1; f.heartbeat(); assert(f.mode() == UiMode::METER);
    f.heartbeat(false); assert(f.mode() == UiMode::VIEW);
    f.now += 60000; f.controller.tick(f.now, f.state); assert(f.mode() == UiMode::VIEW);
  }
  { // Wrapper publishes meter state through the bus and consumes the wake input.
    Wrapper f; f.ready(); f.heartbeat();
    fake_millis = 15000; f.heartbeat(); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_mode == UiMode::METER);
    f.press(false); assert(f.bus.snapshot().ui_mode == UiMode::VIEW && f.requests.empty());
    f.press(false); assert(f.requests.size() == 1 && f.requests[0].type == EventType::REQUEST_READ_CONFIG);
  }

  { // Independent holds: entering/reviewing a draft performs no BLE operation.
    Pure f; f.confirmation();
    f.press(true, true);
    assert(f.mode() == UiMode::SUBMITTING && f.requests.size() == 1);
    assert(f.requests[0].type == EventType::REQUEST_APPLY_CONFIG);
    assert(f.requests[0].voltage == 58.3f && f.requests[0].current == 5.1f);
    f.press(true, true); f.press(false); assert(f.requests.size() == 1);
    Event terminal{}; terminal.type = EventType::STATUS; terminal.request_id = 1; terminal.result = Result::VERIFIED;
    f.controller.observe(terminal, f.state, f.now);
    assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_notice == UiNotice::APPLIED);
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
    assert(f.controller.ui_event().ui_notice == UiNotice::REFRESH_PENDING);
    Event result{}; result.type = EventType::STATUS; result.request_id = 1; result.result = Result::VERIFIED;
    f.controller.observe(result, f.state, f.now);
    assert(f.controller.ui_event().ui_notice == UiNotice::REFRESHED);
  }
  { // At the hold threshold we only advertise the action; release is the sole trigger.
    Pure f;
    f.edge(true, true); f.now = 799; f.controller.tick(f.now, f.state);
    assert(f.controller.ui_event().ui_hold == UiHold::NONE && f.mode() == UiMode::VIEW);
    f.now = 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::EDIT && f.mode() == UiMode::VIEW && f.requests.empty());
    f.edge(true, false); assert(f.mode() == UiMode::EDIT);
    f.press(true); f.edge(true, true); f.now += 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::REVIEW && f.mode() == UiMode::EDIT && f.requests.empty());
    f.edge(true, false); assert(f.mode() == UiMode::CONFIRM);
    f.edge(true, true); f.now += 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::APPLY && f.requests.empty());
    for (int i = 0; i < 10; ++i) { f.now += 100; f.heartbeat(); assert(f.requests.empty()); }
    f.edge(true, false);
    assert(f.requests.size() == 1 && f.controller.ui_event().ui_hold == UiHold::NONE);
  }
  { // Offline B connects without an LCD; only its own result completes it.
    Pure f; f.display_enabled = false; f.heartbeat(false);
    f.state.ready = f.state.connected = f.state.connection_enabled = false;
    f.press(false);
    assert(f.mode() == UiMode::CONNECTING && f.controller.ui_event().ui_notice == UiNotice::CONNECT_PENDING);
    assert(f.requests.size() == 1 && f.requests[0].type == EventType::REQUEST_CONNECT);
    assert(std::isnan(f.requests[0].voltage) && std::isnan(f.requests[0].current));
    f.press(false); f.press(true, true); assert(f.requests.size() == 1);
    Event result{}; result.type = EventType::STATUS; result.result = Result::VERIFIED;
    result.request_id = 0; f.controller.observe(result, f.state, f.now);
    assert(f.mode() == UiMode::CONNECTING);
    result.request_id = 1; f.controller.observe(result, f.state, f.now);
    assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_notice == UiNotice::CONNECTED);
  }
  { // Failed connect is not mislabelled as a refresh or an applied setting.
    Pure f; f.state.ready = f.state.connected = false; f.press(false);
    Event result{}; result.type = EventType::STATUS; result.result = Result::FAILED; result.request_id = 1;
    f.controller.observe(result, f.state, f.now);
    assert(f.controller.ui_event().ui_notice == UiNotice::CONNECT_FAILED);
    assert(f.mode() == UiMode::VIEW);
  }
  for (bool connected : {false, true}) {
    Pure f; f.state.ready = false; f.state.connected = connected; f.state.connection_enabled = true;
    for (int i = 0; i < 4; ++i) f.press(false);
    assert(f.requests.empty() && f.controller.ui_event().ui_notice == UiNotice::NOT_READY);
  }
  { // Help's hold is only a hint until release; leaving consumes either short key.
    Pure f; f.edge(false, true); f.now += 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::HELP && f.mode() == UiMode::VIEW && f.requests.empty());
    f.edge(false, false); assert(f.mode() == UiMode::HELP);
    f.press(true, true); f.press(false, true);
    assert(f.mode() == UiMode::HELP && f.requests.empty());
    const auto selected = f.controller.ui_event().ui_field;
    f.press(true); assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_field == selected);
    f.press(false, true); assert(f.mode() == UiMode::HELP);
    f.press(false); assert(f.mode() == UiMode::VIEW && f.requests.empty());
    f.press(false, true); f.now += 30000; f.heartbeat(); f.controller.tick(f.now, f.state);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // Help is unavailable without a live display; no hidden command is issued.
    Pure f; f.display_enabled = false; f.heartbeat(false); f.press(false, true);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
    assert(f.controller.ui_event().ui_notice == UiNotice::DISPLAY_UNAVAILABLE);
  }
  { // Cancelling the mode while A is held invalidates that entire gesture.
    Pure f; f.confirmation(); f.edge(true, true); f.now += 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::APPLY);
    f.state.busy = true; f.controller.tick(f.now, f.state);
    assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_notice == UiNotice::BUSY);
    f.state.busy = false; f.edge(true, false);
    assert(f.mode() == UiMode::VIEW && f.requests.empty());
  }
  { // A press begun while waiting cannot become an edit when the result arrives mid-hold.
    Pure f; f.press(false); assert(f.mode() == UiMode::REFRESHING);
    f.edge(true, true); f.now += 800;
    Event result{}; result.type = EventType::STATUS; result.request_id = 1; result.result = Result::VERIFIED;
    f.controller.observe(result, f.state, f.now); f.heartbeat();
    assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_hold == UiHold::NONE);
    f.edge(true, false);
    assert(f.mode() == UiMode::VIEW && f.requests.size() == 1);
  }
  { // B's long hold previews cancellation; an overlong A hold never submits.
    Pure f; f.confirmation(); f.edge(false, true); f.now += 800; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::CANCEL && f.mode() == UiMode::CONFIRM);
    f.edge(false, false);
    assert(f.mode() == UiMode::VIEW && f.controller.ui_event().ui_notice == UiNotice::CANCELLED);
    f.confirmation(); f.edge(true, true); f.now += 5001; f.heartbeat();
    assert(f.controller.ui_event().ui_hold == UiHold::RELEASE && f.requests.empty());
    f.edge(true, false); assert(f.requests.empty());
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
    Pure f; f.sequence = 0xfffffffeu; f.now = 0xffffff00u;
    // Start with an input edge so the artificial 49-day jump is not an idle period.
    f.edge(true, true); f.heartbeat(); f.now += 800; f.heartbeat(); f.edge(true, false);
    assert(f.mode() == UiMode::EDIT);
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
  { // Local UI feedback ages normally; background traffic cannot renew or replace it.
    Wrapper f; f.ready(); f.heartbeat(); fake_millis = 1000; f.press(true);
    assert(f.bus.snapshot().ui_notice == UiNotice::SELECTED);
    const uint32_t updated = f.bus.snapshot().ui_updated_at;
    fake_millis += 5000; f.heartbeat();
    Event background{}; background.type = EventType::STATUS;
    background.request_id = 0; background.result = Result::VERIFIED; f.emit(background);
    f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_notice == UiNotice::SELECTED && f.bus.snapshot().ui_updated_at == updated);
    assert(fake_millis - updated >= 3000); // The display can expire an ordinary notice.
    f.press(false); assert(f.requests.size() == 1);
    Event result{}; result.type = EventType::STATUS;
    result.request_id = f.requests[0].request_id; result.result = Result::UNKNOWN;
    f.emit(result); f.buttons.loop(); f.drain();
    const uint32_t failed_at = f.bus.snapshot().ui_updated_at;
    assert(f.bus.snapshot().ui_notice == UiNotice::UNKNOWN);
    fake_millis += 5000; f.heartbeat(); f.emit(background); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_notice == UiNotice::UNKNOWN && f.bus.snapshot().ui_updated_at == failed_at);
    f.press(true); assert(f.bus.snapshot().ui_notice == UiNotice::SELECTED);
  }
  { // Actual wrapper routes offline connection once and Help without a command.
    Wrapper f;
    f.press(false, false, false);
    assert(f.bus.snapshot().ui_mode == UiMode::CONNECTING && f.requests.size() == 1);
    assert(f.requests[0].type == EventType::REQUEST_CONNECT);
    Event result{}; result.type = EventType::STATUS; result.request_id = f.requests[0].request_id;
    result.result = Result::FAILED; f.emit(result); f.buttons.loop(); f.drain();
    assert(f.bus.snapshot().ui_notice == UiNotice::CONNECT_FAILED);
    f.heartbeat(); f.press(false, true); assert(f.bus.snapshot().ui_mode == UiMode::HELP);
    f.press(false); assert(f.bus.snapshot().ui_mode == UiMode::VIEW && f.requests.size() == 1);
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
  { // LED has only link semantics, independent of operations, telemetry and results.
    charger_indicator::IndicatorController led;
    Snapshot state;
    assert(!led.level(state, 0) && !led.level(state, 500));
    state.connection_enabled = true;
    assert(led.level(state, 0) && led.level(state, 499) && !led.level(state, 500));
    assert(!led.level(state, 999) && led.level(state, 1000));
    for (bool connected : {false, true}) {
      for (bool enabled : {false, true}) {
        state.connected = connected; state.connection_enabled = enabled;
        for (bool busy : {false, true}) {
          for (bool ready : {false, true}) {
            state.busy = busy; state.ready = ready;
            for (auto result : {Result::INFO, Result::ACCEPTED, Result::VERIFIED,
                                Result::REJECTED, Result::FAILED, Result::UNKNOWN}) {
              state.result = result; state.telemetry_valid = !ready;
              assert(led.level(state, 250) == (connected || enabled));
              assert(led.level(state, 750) == connected);
            }
          }
        }
      }
    }
  }
  { // LED wrapper works without buttons; initialization and error events cannot blink a connected link.
    Wrapper f(false); f.indicator.loop(); assert(!f.led.state);
    Event event{}; event.type = EventType::CONNECTION; event.connection_enabled = true;
    f.emit(event); f.indicator.loop(); assert(f.led.state);
    fake_millis = 500; f.indicator.loop(); assert(!f.led.state);
    event.connected = true; event.ready = false; f.emit(event);
    f.indicator.loop(); assert(f.led.state);
    event = Event{}; event.type = EventType::STATUS; event.busy = true; event.result = Result::UNKNOWN;
    f.emit(event); fake_millis = 750; f.indicator.loop(); assert(f.led.state);
    const unsigned writes = f.led.writes;
    f.indicator.loop(); assert(f.led.writes == writes && f.requests.empty());
    event = Event{}; event.type = EventType::CONNECTION; f.emit(event);
    f.indicator.loop(); assert(!f.led.state);
  }
  puts("PASS: pure and actual button/LED wrappers, explicit single Apply, bounds, no-LCD read-only, heartbeat, cancellation, chord/lost-edge/wrap, UI overflow and connection-only LED");
}
