#include "esphome/components/charger_web/charger_web.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace esphome;
using namespace charger_event_bus;

namespace {

struct Fixture {
  ChargerEventBus bus;
  charger_web::ChargerWeb web;
  sensor::Sensor configured_v, configured_a, output_v, output_a;
  binary_sensor::BinarySensor ready, telemetry_valid;
  charger_web::DraftNumber requested_v{&web, true}, requested_a{&web, false};
  charger_web::ConnectionSwitch connection_switch{&web};
  text_sensor::TextSensor status, raw, link_status, output_status;
  unsigned optional;

  explicit Fixture(unsigned mask = 31) : optional(mask) {
    fake_millis = 0;
    web.set_event_bus(&bus);
    web.set_configured_voltage(&configured_v);
    web.set_configured_current(&configured_a);
    web.set_requested_voltage(&requested_v);
    web.set_requested_current(&requested_a);
    web.set_connection(&ready);
    web.set_connection_switch(&connection_switch);
    web.set_transaction_status(&status);
    web.set_raw_status(&raw);
    if (optional & 1) web.set_output_voltage(&output_v);
    if (optional & 2) web.set_output_current(&output_a);
    if (optional & 4) web.set_telemetry_valid(&telemetry_valid);
    if (optional & 8) web.set_link_status(&link_status);
    if (optional & 16) web.set_output_data_status(&output_status);
    bus.setup();
    web.setup();
    assert(!web.is_failed());
  }

  void drain() { for (int i = 0; i < 8; ++i) bus.loop(); }
  void emit(const Event &event) { assert(bus.publish(event)); drain(); }
  void tick(uint32_t at) { fake_millis = at; drain(); web.loop(); }
  void connect(bool connected = true, bool ready = true, bool enabled = true) {
    Event event{}; event.type = EventType::CONNECTION;
    event.connected = connected; event.ready = ready; event.connection_enabled = enabled;
    emit(event);
  }
  void capability(bool supported = true) {
    Event event{}; event.type = EventType::TELEMETRY_CAPABILITY;
    event.telemetry_supported = supported; emit(event);
  }
  void config(float v = 58.4f, float a = 5.1f) {
    Event event{}; event.type = EventType::CONFIG; event.voltage = v; event.current = a;
    emit(event);
  }
  void measure(float v, float a, uint32_t sampled, bool valid = true) {
    Event event{}; event.type = EventType::TELEMETRY;
    event.output_voltage = v; event.output_current = a;
    event.sampled_at = sampled; event.telemetry_valid = valid;
    emit(event);
  }
  void assert_unknown() const {
    assert(std::isnan(output_v.state) && std::isnan(output_a.state));
    assert(!telemetry_valid.state);
    if (!(optional & 1)) assert(output_v.states.empty());
    if (!(optional & 2)) assert(output_a.states.empty());
    if (!(optional & 4)) assert(telemetry_valid.states.empty());
    if (!(optional & 8)) assert(link_status.states.empty());
    if (!(optional & 16)) assert(output_status.states.empty());
  }
  void assert_measured(float v, float a) const {
    if (optional & 1) assert(output_v.state == v);
    if (optional & 2) assert(output_a.state == a);
    if (optional & 4) assert(telemetry_valid.state);
  }
};

void test_boot_and_config_are_not_measurements() {
  Fixture f;
  f.assert_unknown();
  assert(f.output_v.states.size() == 1 && f.output_a.states.size() == 1);
  assert(f.telemetry_valid.states.size() == 1);
  f.connect(); f.config(); f.tick(1000);
  f.assert_unknown();
  assert(f.configured_v.state == 58.4f && f.configured_a.state == 5.1f);
  assert(f.output_v.states.size() == 1 && f.output_a.states.size() == 1);
  assert(f.web.stage(true, 58.2f)); f.drain();
  f.web.apply(); f.drain();  // No BLE owner: the request alone cannot publish results.
  f.assert_unknown();
  assert(f.configured_v.state == 58.4f && f.configured_a.state == 5.1f);
  Event raw{}; raw.type = EventType::RAW_STATUS;
  raw.message = "5e5e1184000000000100000000001c1c00000094";
  f.emit(raw);
  f.assert_unknown();
  assert(f.raw.state == raw.message);
}

void test_real_publication_stale_and_recovery() {
  Fixture f;
  f.connect(); f.capability(); f.config(); f.tick(1000);
  f.measure(52.7f, 3.2f, 1000);  // Typed synthetic telemetry; no device decoder is implied.
  f.assert_measured(52.7f, 3.2f);
  assert(f.configured_v.state == 58.4f && f.configured_a.state == 5.1f);
  const auto output_count = f.output_v.states.size();
  f.tick(6999); f.assert_measured(52.7f, 3.2f);
  assert(f.output_v.states.size() == output_count);
  f.config(58.2f, 4.9f);  // A fresh CONFIG does not refresh output age.
  f.tick(7000); f.assert_unknown();
  assert(f.output_v.states.size() == output_count + 1);
  assert(f.configured_v.state == 58.2f && f.configured_a.state == 4.9f);
  f.tick(7001); f.assert_unknown();
  assert(f.output_v.states.size() == output_count + 1);  // Do not spam NAN each loop.
  f.measure(52.8f, 3.1f, 7001); f.assert_measured(52.8f, 3.1f);
  f.measure(0.0f, 0.0f, 7001); f.assert_measured(0.0f, 0.0f);
  assert(f.telemetry_valid.state);  // Real, explicit zero differs from unavailable.
}

void test_disconnect_and_late_telemetry_cannot_revive_output() {
  Fixture f;
  f.connect(); f.capability(); f.config(); f.tick(1000);
  f.measure(52.7f, 3.2f, 1000);
  f.connect(false, true);  // Even inconsistent event bits use normalized ready.
  f.tick(1001); f.assert_unknown();
  assert(!f.ready.state && !f.connection_switch.state);
  assert(std::isnan(f.configured_v.state) && std::isnan(f.configured_a.state));
  f.measure(52.7f, 3.2f, 1001); f.assert_unknown();
  f.config();
  assert(std::isnan(f.configured_v.state) && std::isnan(f.configured_a.state));
  f.connect(); f.config(); f.tick(1002); f.assert_unknown();
  f.measure(52.9f, 3.0f, 1002); f.assert_measured(52.9f, 3.0f);
}

void test_invalid_or_already_stale_samples_publish_nan() {
  const float infinity = std::numeric_limits<float>::infinity();
  for (int reason = 0; reason < 7; ++reason) {
    Fixture f;
    f.connect(); f.capability(); f.config(); f.tick(10000);
    f.measure(52.7f, 3.2f, 10000); f.assert_measured(52.7f, 3.2f);
    switch (reason) {
      case 0: f.measure(NAN, 3.2f, 10000); break;
      case 1: f.measure(52.7f, NAN, 10000); break;
      case 2: f.measure(infinity, 3.2f, 10000); break;
      case 3: f.measure(52.7f, -infinity, 10000); break;
      case 4: f.measure(52.7f, 3.2f, 10000, false); break;
      case 5: f.measure(52.7f, 3.2f, 4000); break;  // Already 6000 ms old at delivery.
      case 6: f.measure(52.7f, 3.2f, 10001); break;  // Future sample is invalid for now.
    }
    f.assert_unknown();
    assert(f.configured_v.state == 58.4f && f.configured_a.state == 5.1f);
  }
}

void test_wraparound_and_optional_entities() {
  for (unsigned mask = 0; mask < 32; ++mask) {
    Fixture f(mask);
    f.assert_unknown(); f.connect(); f.capability(); f.config();
    const uint32_t at = std::numeric_limits<uint32_t>::max() - 100;
    f.tick(at); f.measure(52.7f, 3.2f, at);
    f.tick(200); f.assert_measured(52.7f, 3.2f);
    if (mask & 8) assert(f.link_status.state == "Connected (ready)");
    if (mask & 16) assert(f.output_status.state == "Live measured output");
    f.tick(uint32_t(at + 5999)); f.assert_measured(52.7f, 3.2f);
    f.tick(uint32_t(at + 6000)); f.assert_unknown();
    if (mask & 16) assert(f.output_status.state == "Output sample expired");
    f.measure(52.7f, 3.2f, fake_millis); f.assert_measured(52.7f, 3.2f);
    f.connect(false, false); f.web.loop(); f.assert_unknown();
    assert(std::isnan(f.configured_v.state) && std::isnan(f.configured_a.state));
    if (mask & 8) assert(f.link_status.state == "Connecting");
    if (mask & 16) assert(f.output_status.state == "Waiting for BLE connection");
  }
}

void test_connection_and_decoding_status_are_separate() {
  Fixture f;
  f.tick(1000);
  assert(f.link_status.state == "Disconnected / disabled");
  assert(f.output_status.state == "Waiting for BLE connection");
  f.assert_unknown();
  f.web.connect(true); f.drain(); f.tick(1000);
  assert(f.link_status.state == "Disconnected / disabled");  // Request is only intent.
  assert(!f.connection_switch.state);
  f.connect(false, false, true); f.tick(1000);
  assert(f.link_status.state == "Connecting" && !f.connection_switch.state);
  assert(f.output_status.state == "Waiting for BLE connection");
  f.connect(true, false); f.tick(1000);
  assert(f.link_status.state == "Connected (initializing)" && f.connection_switch.state);
  assert(f.output_status.state == "BLE connected; initializing");
  f.connect(); f.config(); f.tick(1000);
  assert(f.link_status.state == "Connected (ready)" && f.ready.state);
  assert(f.output_status.state == "BLE connected; output decoding not implemented");
  f.assert_unknown();
  const auto link_count = f.link_status.states.size();
  const auto output_count = f.output_status.states.size();
  f.tick(1001);
  assert(f.link_status.states.size() == link_count && f.output_status.states.size() == output_count);

  Event raw{}; raw.type = EventType::RAW_STATUS; raw.sampled_at = 1001;
  raw.message = "5e5e1184000000000000000000001c1c00000095";
  f.emit(raw); f.tick(1001);
  assert(f.output_status.state == "BLE connected; output decoding not implemented");
  f.assert_unknown();
  f.measure(0.0f, 0.0f, 1001); f.tick(1001);  // No verified decoder capability.
  f.assert_unknown();
  assert(f.output_status.state == "BLE connected; output decoding not implemented");
  f.capability(); f.tick(1001);
  assert(f.output_status.state == "Waiting for first output sample");
  f.measure(0.0f, 0.0f, 1001, false); f.tick(1001);
  assert(f.output_status.state == "Invalid output sample");
  f.assert_unknown();
  f.measure(0.0f, 0.0f, 1001); f.tick(1001);
  assert(f.output_status.state == "Live measured output");
  f.assert_measured(0.0f, 0.0f);
  f.tick(7001);
  assert(f.output_status.state == "Output sample expired");
  assert(f.link_status.state == "Connected (ready)");
  f.assert_unknown();
  f.measure(52.7f, 3.2f, 7001); f.tick(7001);
  assert(f.output_status.state == "Live measured output");
  f.assert_measured(52.7f, 3.2f);
  f.capability(false); f.tick(7002);
  assert(f.output_status.state == "BLE connected; output decoding not implemented");
  f.assert_unknown();
  f.measure(0.0f, 0.0f, 7002); f.tick(7002);
  f.assert_unknown();
  assert(f.output_status.state == "BLE connected; output decoding not implemented");
  f.connect(false, true, false); f.tick(7003);
  assert(f.link_status.state == "Disconnected / disabled" && !f.ready.state);
  assert(f.output_status.state == "Waiting for BLE connection");
  f.assert_unknown();
}

}  // namespace

int main() {
  test_boot_and_config_are_not_measurements();
  test_real_publication_stale_and_recovery();
  test_disconnect_and_late_telemetry_cannot_revive_output();
  test_invalid_or_already_stale_samples_publish_nan();
  test_wraparound_and_optional_entities();
  test_connection_and_decoding_status_are_separate();
  std::puts("PASS: real Web connection/decoding states, capability gating, 6s stale/disconnect/NAN, wrap, 32 optional-entity combinations");
}
