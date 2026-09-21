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

void connect(EventCore &bus, bool connected = true, bool ready = true) {
  Event event;
  event.type = EventType::CONNECTION;
  event.connected = connected;
  event.ready = ready;
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
  assert(!contains(make_view(snapshot, now), "LIVE") || contains(make_view(snapshot, now), "NO LIVE DATA"));
}

void assert_same_ble(const Snapshot &before, const Snapshot &after) {
  assert(after.connected == before.connected && after.ready == before.ready && after.busy == before.busy);
  assert(after.voltage == before.voltage && after.current == before.current);
  assert(after.output_voltage == before.output_voltage && after.output_current == before.output_current);
  assert(after.telemetry_valid == before.telemetry_valid && after.sampled_at == before.sampled_at);
  assert(after.status == before.status && after.result == before.result);
  assert(after.raw_status == before.raw_status && after.last_request_id == before.last_request_id);
}

void test_config_and_requests_never_create_output() {
  EventCore bus;
  assert_unknown_output(bus.snapshot(), 0);
  connect(bus);
  configure(bus);
  assert_unknown_output(bus.snapshot(), 1000);
  const View view = make_view(bus.snapshot(), 1000);
  assert(contains(view, "Set 58.4 V") && contains(view, "Set 5.1 A"));
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
  configure(bus);
  // Synthetic physical values exercise the typed event contract, not any
  // unverified device-protocol offset or scaling assumption.
  deliver(bus, telemetry(52.7f, 3.2f, 1000));
  assert(bus.snapshot().telemetry_fresh(1000));
  auto view = make_view(bus.snapshot(), 1000);
  assert((large_values(view) == std::vector<std::string>{"52.7", "3.2"}));
  assert(contains(view, "Set 58.4 V") && contains(view, "Set 5.1 A"));
  configure(bus, 58.2f, 4.9f);
  view = make_view(bus.snapshot(), 1100);
  assert((large_values(view) == std::vector<std::string>{"52.7", "3.2"}));
  assert(contains(view, "Set 58.2 V") && contains(view, "Set 4.9 A"));
  configure(bus);
  assert_unknown_output(bus.snapshot(), 7000);  // Fresh config cannot extend telemetry life.

  deliver(bus, telemetry(0.0f, 0.0f, 1200));
  assert(bus.snapshot().telemetry_fresh(1200));  // An explicit valid zero is not missing data.
  assert((large_values(make_view(bus.snapshot(), 1200)) == std::vector<std::string>{"0.0", "0.0"}));
}

void test_freshness_boundary_and_wrap() {
  EventCore bus;
  connect(bus);
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
    deliver(bus, event);
    assert_same_ble(original, bus.snapshot());
  }
  assert(bus.snapshot().ui_display_ready);
  assert(bus.snapshot().ui_buttons_ready);
  assert(bus.snapshot().ui_mode == UiMode::EDIT);
  assert(bus.snapshot().ui_voltage == 58.2f && bus.snapshot().ui_current == 4.9f);
  auto editing = make_view(bus.snapshot(), 1000);
  assert(contains(editing, "SET VOLTAGE") && contains(editing, "SET CURRENT"));
  assert(!contains(editing, "OUTPUT"));
  Event display_failure;
  display_failure.type = EventType::UI_DISPLAY;
  display_failure.ui_display_ready = false;
  deliver(bus, display_failure);
  assert(!bus.snapshot().ui_display_ready);
  assert_same_ble(original, bus.snapshot());
}

void test_optional_controls_do_not_advertise_missing_buttons() {
  EventCore bus;
  connect(bus);
  configure(bus);
  auto view = make_view(bus.snapshot(), 1000);
  assert(!contains(view, "A:") && !contains(view, "B:"));
  assert(contains(view, "Set 58.4 V") && contains(view, "Set 5.1 A"));
  Event controls;
  controls.type = EventType::UI_CONTROLS;
  controls.ui_buttons_ready = true;
  deliver(bus, controls);
  view = make_view(bus.snapshot(), 1000);
  assert(contains(view, "A:") && contains(view, "B:"));
  controls.ui_buttons_ready = false;
  deliver(bus, controls);
  view = make_view(bus.snapshot(), 1000);
  assert(!contains(view, "A:") && !contains(view, "B:"));
  assert_unknown_output(bus.snapshot(), 1000);
}

void test_observers_render_reduced_state() {
  EventCore bus;
  connect(bus);
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
  test_optional_controls_do_not_advertise_missing_buttons();
  test_observers_render_reduced_state();
  std::puts("PASS: telemetry freshness, output/config separation, and UI event isolation (real core + view)");
}
