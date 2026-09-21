#include "charger_event_bus/event_core.h"
#include "charger_display/charger_display.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace esphome::charger_event_bus;
using esphome::charger_display::Font;
using esphome::charger_display::View;
using esphome::charger_display::make_view;

namespace {

void deliver(EventCore &bus, const Event &event) {
  assert(bus.publish(event));
  while (bus.pending()) assert(bus.dispatch() > 0);
}

void connect(EventCore &bus, bool connected = true, bool ready = true, bool enabled = true) {
  Event event;
  event.type = EventType::CONNECTION;
  event.connected = connected;
  event.connection_enabled = enabled;
  event.ready = ready;
  deliver(bus, event);
}

void capability(EventCore &bus, bool supported = true) {
  Event event;
  event.type = EventType::TELEMETRY_CAPABILITY;
  event.telemetry_supported = supported;
  deliver(bus, event);
}

void configure(EventCore &bus, float voltage = 58.4f, float current = 5.1f) {
  Event event;
  event.type = EventType::CONFIG;
  event.voltage = voltage;
  event.current = current;
  event.request_id = 21;
  deliver(bus, event);
}

Event telemetry(float voltage, float current, uint32_t at, bool valid = true) {
  Event event;
  event.type = EventType::TELEMETRY;
  event.output_voltage = voltage;
  event.output_current = current;
  event.sampled_at = at;
  event.telemetry_valid = valid;
  return event;
}

std::vector<std::string> large_values(const View &view) {
  std::vector<std::string> values;
  for (size_t i = 0; i < view.count; ++i)
    if (view.labels[i].font == Font::LARGE) values.push_back(view.labels[i].text);
  return values;
}

bool contains(const View &view, const std::string &text) {
  for (size_t i = 0; i < view.count; ++i)
    if (view.labels[i].text.find(text) != std::string::npos) return true;
  return false;
}

void assert_unknown_output(const Snapshot &snapshot, uint32_t now) {
  assert(!snapshot.telemetry_fresh(now));
  assert((large_values(make_view(snapshot, now)) == std::vector<std::string>{"--.-", "--.-"}));
  assert(snapshot.output_state(now) != OutputState::LIVE);
}

void assert_same_ble(const Snapshot &before, const Snapshot &after) {
  const auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
  assert(after.connected == before.connected && after.connection_enabled == before.connection_enabled);
  assert(after.ready == before.ready && after.busy == before.busy);
  assert(same(after.voltage, before.voltage) && same(after.current, before.current));
  assert(same(after.output_voltage, before.output_voltage) && same(after.output_current, before.output_current));
  assert(after.telemetry_valid == before.telemetry_valid && after.sampled_at == before.sampled_at);
  assert(after.telemetry_supported == before.telemetry_supported && after.telemetry_seen == before.telemetry_seen);
  assert(after.raw_status_seen == before.raw_status_seen && after.raw_sampled_at == before.raw_sampled_at);
  assert(after.status == before.status && after.result == before.result);
  assert(after.raw_status == before.raw_status && after.last_request_id == before.last_request_id);
}

void test_config_and_requests_never_create_output() {
  EventCore bus;
  assert_unknown_output(bus.snapshot(), 0);
  connect(bus);
  capability(bus);
  configure(bus);
  assert_unknown_output(bus.snapshot(), 1000);
  const View view = make_view(bus.snapshot(), 1000);
  assert(contains(view, "58.4 V") && contains(view, "5.1 A"));
  assert(std::isnan(bus.snapshot().output_voltage) && std::isnan(bus.snapshot().output_current));

  assert(bus.request(EventType::REQUEST_APPLY_CONFIG, "test", 58.2f, 4.9f) != 0);
  bus.dispatch();
  assert(bus.snapshot().voltage == 58.4f && bus.snapshot().current == 5.1f);
  assert_unknown_output(bus.snapshot(), 1000);
  Event raw;
  raw.type = EventType::RAW_STATUS;
  raw.message = "5e5e1184000000000100000000001c1c00000094";
  deliver(bus, raw);
  assert_unknown_output(bus.snapshot(), 1000);  // Raw bytes have no implicit physical-unit decoder.
}

void test_measurements_stay_separate_from_config() {
  EventCore bus;
  connect(bus);
  capability(bus);
  configure(bus);
  // Synthetic physical values exercise the typed event contract, not any
  // unverified device-protocol offset or scaling assumption.
  deliver(bus, telemetry(52.7f, 3.2f, 1000));
  assert(bus.snapshot().telemetry_fresh(1000));
  auto view = make_view(bus.snapshot(), 1000);
  assert((large_values(view) == std::vector<std::string>{"52.7", "3.2"}));
  assert(contains(view, "58.4 V") && contains(view, "5.1 A"));
  configure(bus, 58.2f, 4.9f);
  view = make_view(bus.snapshot(), 1100);
  assert((large_values(view) == std::vector<std::string>{"52.7", "3.2"}));
  assert(contains(view, "58.2 V") && contains(view, "4.9 A"));
  configure(bus);
  assert_unknown_output(bus.snapshot(), 7000);  // Fresh config cannot extend telemetry life.

  deliver(bus, telemetry(0.0f, 0.0f, 1200));
  assert(bus.snapshot().telemetry_fresh(1200));  // An explicit valid zero is not missing data.
  assert((large_values(make_view(bus.snapshot(), 1200)) == std::vector<std::string>{"0.0", "0.0"}));
}

void test_freshness_boundary_and_wrap() {
  EventCore bus;
  connect(bus);
  capability(bus);
  deliver(bus, telemetry(52.7f, 3.2f, 1000));
  assert(bus.snapshot().telemetry_fresh(6999));
  assert_unknown_output(bus.snapshot(), 7000);
  assert_unknown_output(bus.snapshot(), 7001);
  assert(!bus.snapshot().telemetry_fresh(1000, 0));
  assert(bus.snapshot().telemetry_fresh(1499, 500));
  assert(!bus.snapshot().telemetry_fresh(1500, 500));
  assert_unknown_output(bus.snapshot(), 999);  // A future timestamp is not current data.

  const uint32_t at = std::numeric_limits<uint32_t>::max() - 100;
  deliver(bus, telemetry(52.7f, 3.2f, at));
  assert(bus.snapshot().telemetry_fresh(200));  // 301 ms old across millis() rollover.
  assert(bus.snapshot().telemetry_fresh(uint32_t(at + 5999)));
  assert_unknown_output(bus.snapshot(), uint32_t(at + 6000));
}

void test_disconnect_and_late_reports() {
  EventCore bus;
  connect(bus);
  capability(bus);
  configure(bus);
  deliver(bus, telemetry(52.7f, 3.2f, 1000));
  connect(bus, false, true);  // Contradictory ready bit must be normalized by the reducer.
  assert(!bus.snapshot().ready && !bus.snapshot().telemetry_valid);
  assert(std::isnan(bus.snapshot().output_voltage) && std::isnan(bus.snapshot().output_current));
  assert_unknown_output(bus.snapshot(), 1001);
  deliver(bus, telemetry(52.7f, 3.2f, 1002));
  configure(bus);
  assert_unknown_output(bus.snapshot(), 1003);
  assert(std::isnan(bus.snapshot().voltage) && std::isnan(bus.snapshot().current));
  connect(bus);
  capability(bus);
  configure(bus);
  assert_unknown_output(bus.snapshot(), 1004);  // Reconnect/config cannot revive a prior sample.
  deliver(bus, telemetry(52.8f, 3.1f, 1005));
  assert(bus.snapshot().telemetry_fresh(1005));
}

void test_invalid_telemetry_clears_both_values() {
  const float infinity = std::numeric_limits<float>::infinity();
  for (const auto &invalid : {telemetry(NAN, 3.2f, 1001), telemetry(52.7f, NAN, 1001),
                            telemetry(infinity, 3.2f, 1001), telemetry(52.7f, infinity, 1001),
                            telemetry(-infinity, 3.2f, 1001), telemetry(52.7f, -infinity, 1001),
                            telemetry(52.7f, 3.2f, 1001, false)}) {
    EventCore bus;
    connect(bus);
  capability(bus);
    configure(bus);
    deliver(bus, telemetry(52.7f, 3.2f, 1000));
    assert(bus.snapshot().telemetry_fresh(1000));
    deliver(bus, invalid);
    assert_unknown_output(bus.snapshot(), 1001);
    assert(std::isnan(bus.snapshot().output_voltage) && std::isnan(bus.snapshot().output_current));
    assert(bus.snapshot().voltage == 58.4f && bus.snapshot().current == 5.1f);
  }
}

void test_ui_events_cannot_change_ble_state() {
  EventCore bus;
  connect(bus);
  capability(bus);
  configure(bus);
  deliver(bus, telemetry(52.7f, 3.2f, 1000));
  Event status;
  status.type = EventType::STATUS;
  status.message = "BLE transaction active";
  status.request_id = 21;
  status.busy = true;
  status.result = Result::ACCEPTED;
  deliver(bus, status);
  const Snapshot original = bus.snapshot();
  for (EventType type : {EventType::UI_DISPLAY, EventType::UI_CONTROLS, EventType::UI_STATE, EventType::INPUT}) {
    Event event;
    event.type = type;
    event.source = "test UI";
    event.connected = false;
    event.ready = false;
    event.busy = false;
    event.voltage = 58.2f;
    event.current = 4.9f;
    event.output_voltage = 99.9f;
    event.output_current = 99.9f;
    event.sampled_at = 2000;
    event.telemetry_valid = false;
    event.result = Result::FAILED;
    event.request_id = 987;
    event.message = "UI feedback only";
    event.ui_display_ready = true;
    event.ui_buttons_ready = true;
    event.ui_mode = UiMode::EDIT;
    event.ui_notice = UiNotice::LIMIT;
    event.ui_hold = UiHold::REVIEW;
    deliver(bus, event);
    assert_same_ble(original, bus.snapshot());
  }
  assert(bus.snapshot().ui_display_ready);
  assert(bus.snapshot().ui_buttons_ready);
  assert(bus.snapshot().ui_mode == UiMode::EDIT);
  assert(bus.snapshot().ui_notice == UiNotice::LIMIT && bus.snapshot().ui_hold == UiHold::REVIEW);
  assert(bus.snapshot().ui_updated_at == 2000 && bus.snapshot().ui_request_id == 987);
  assert(bus.snapshot().ui_voltage == 58.2f && bus.snapshot().ui_current == 4.9f);
  auto editing = make_view(bus.snapshot(), 1000);
  assert((large_values(editing) == std::vector<std::string>{"58.2", "4.9"}));
  assert(!contains(editing, "52.7") && !contains(editing, "3.2"));
  Event display_failure;
  display_failure.type = EventType::UI_DISPLAY;
  display_failure.ui_display_ready = false;
  deliver(bus, display_failure);
  assert(!bus.snapshot().ui_display_ready);
  assert_same_ble(original, bus.snapshot());
}

void test_optional_controls_change_hints_without_changing_data() {
  EventCore bus;
  connect(bus);
  capability(bus);
  configure(bus);
  const Snapshot original = bus.snapshot();
  auto text = [](const View &view) {
    std::string joined;
    for (size_t i = 0; i < view.count; ++i) joined += view.labels[i].text + "\n";
    return joined;
  };
  const auto no_buttons = text(make_view(bus.snapshot(), 1000));
  Event controls;
  controls.type = EventType::UI_CONTROLS;
  controls.ui_buttons_ready = true;
  deliver(bus, controls);
  assert(bus.snapshot().ui_buttons_ready);
  assert(text(make_view(bus.snapshot(), 1000)) != no_buttons);
  controls.ui_buttons_ready = false;
  deliver(bus, controls);
  assert(!bus.snapshot().ui_buttons_ready);
  assert(text(make_view(bus.snapshot(), 1000)) == no_buttons);
  assert_same_ble(original, bus.snapshot());
  assert_unknown_output(bus.snapshot(), 1000);
}

void test_connection_intent_and_physical_link_are_independent() {
  EventCore bus;
  assert(!bus.snapshot().connected && !bus.snapshot().connection_enabled);
  assert(bus.snapshot().output_state(1000) == OutputState::DISCONNECTED);
  assert(bus.request(EventType::REQUEST_CONNECT, "test") != 0);
  bus.dispatch();
  assert(!bus.snapshot().connected && !bus.snapshot().connection_enabled);
  connect(bus, false, false, true);  // BLE enabled/scanning is not a connection.
  assert(!bus.snapshot().connected && bus.snapshot().connection_enabled);
  assert(bus.snapshot().output_state(1000) == OutputState::DISCONNECTED);
  assert_unknown_output(bus.snapshot(), 1000);
  connect(bus, true, false, true);
  assert(bus.snapshot().output_state(1000) == OutputState::INITIALIZING);
  connect(bus, true, true, false);  // Disabled intent does not invent a physical disconnect.
  assert(bus.snapshot().connected && !bus.snapshot().connection_enabled);
  assert(bus.snapshot().output_state(1000) == OutputState::UNSUPPORTED);
  connect(bus, false, true, false);
  assert(!bus.snapshot().ready && !bus.snapshot().busy);
  assert(bus.snapshot().output_state(1000) == OutputState::DISCONNECTED);
}

void test_output_state_transitions_and_raw_samples() {
  EventCore bus;
  connect(bus);
  configure(bus);
  assert(bus.snapshot().output_state(1000) == OutputState::UNSUPPORTED);
  assert_unknown_output(bus.snapshot(), 1000);
  Event raw;
  raw.type = EventType::RAW_STATUS;
  raw.message = "5e5e1184000000000000000000001c1c00000095";
  raw.sampled_at = 1000;
  deliver(bus, raw);
  assert(bus.snapshot().raw_status_seen && bus.snapshot().raw_sampled_at == 1000);
  assert(!bus.snapshot().telemetry_seen && !bus.snapshot().telemetry_valid);
  assert(bus.snapshot().output_state(1000) == OutputState::UNSUPPORTED);
  assert_unknown_output(bus.snapshot(), 1000);

  capability(bus);
  assert(bus.snapshot().output_state(1000) == OutputState::WAITING);
  assert_unknown_output(bus.snapshot(), 1000);
  deliver(bus, telemetry(0.0f, 0.0f, 1000, false));
  assert(bus.snapshot().output_state(1000) == OutputState::INVALID);
  assert_unknown_output(bus.snapshot(), 1000);
  deliver(bus, telemetry(0.0f, 0.0f, 1001));
  assert(bus.snapshot().output_state(1001) == OutputState::LIVE);
  assert((large_values(make_view(bus.snapshot(), 1001)) == std::vector<std::string>{"0.0", "0.0"}));
  assert(bus.snapshot().output_state(7000) == OutputState::LIVE);
  assert(bus.snapshot().output_state(7001) == OutputState::STALE);
  assert_unknown_output(bus.snapshot(), 7001);
  raw.sampled_at = 7001;
  deliver(bus, raw);
  assert(bus.snapshot().raw_sampled_at == 7001 && bus.snapshot().sampled_at == 1001);
  assert(bus.snapshot().output_state(7001) == OutputState::STALE);
  configure(bus);
  assert(bus.snapshot().output_state(7001) == OutputState::STALE);

  capability(bus, false);
  assert(bus.snapshot().output_state(7001) == OutputState::UNSUPPORTED);
  assert(std::isnan(bus.snapshot().output_voltage) && std::isnan(bus.snapshot().output_current));
  assert(!bus.snapshot().telemetry_valid && !bus.snapshot().telemetry_seen);
  deliver(bus, telemetry(0.0f, 0.0f, 7002));  // A delayed producer cannot bypass unavailable decoder capability.
  assert(bus.snapshot().output_state(7002) == OutputState::UNSUPPORTED);
  assert_unknown_output(bus.snapshot(), 7002);
  capability(bus);
  assert(bus.snapshot().output_state(7002) == OutputState::WAITING);
  deliver(bus, telemetry(52.7f, 3.2f, 7003));
  assert(bus.snapshot().output_state(7003) == OutputState::LIVE);
  connect(bus, false);
  assert(!bus.snapshot().raw_status_seen && !bus.snapshot().telemetry_seen);
  raw.sampled_at = 7004;
  deliver(bus, raw);
  assert(!bus.snapshot().raw_status_seen);
  connect(bus);
  assert(bus.snapshot().output_state(7004) == OutputState::WAITING);
  assert_unknown_output(bus.snapshot(), 7004);
}

void test_observers_render_reduced_state() {
  EventCore bus;
  connect(bus);
  capability(bus);
  configure(bus);
  int observed = 0;
  assert(bus.subscribe([&](const Event &event) {
    if (event.type != EventType::TELEMETRY) return;
    ++observed;
    assert(bus.snapshot().telemetry_fresh(1000));
    assert((large_values(make_view(bus.snapshot(), 1000)) == std::vector<std::string>{"52.7", "3.2"}));
  }));
  assert(bus.publish(telemetry(52.7f, 3.2f, 1000)));
  assert_unknown_output(bus.snapshot(), 1000);
  assert(observed == 0);
  bus.dispatch();
  assert(observed == 1);
}

}  // namespace

int main() {
  test_config_and_requests_never_create_output();
  test_measurements_stay_separate_from_config();
  test_freshness_boundary_and_wrap();
  test_disconnect_and_late_reports();
  test_invalid_telemetry_clears_both_values();
  test_ui_events_cannot_change_ble_state();
  test_optional_controls_change_hints_without_changing_data();
  test_connection_intent_and_physical_link_are_independent();
  test_output_state_transitions_and_raw_samples();
  test_observers_render_reduced_state();
  std::puts("PASS: connection intent/link isolation, telemetry capability/states/freshness, output/config separation, raw timestamp and UI event isolation (real core + view)");
}
