#include "esphome/components/lxy_charger/lxy_charger.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace esphome;
using namespace esphome::charger_event_bus;
namespace protocol = esphome::lxy_charger::protocol;

class TestCharger : public lxy_charger::LXYCharger {
 public:
  void attach(ble_client::BLEClient *parent) { parent_ = parent; }
  // Model a completed discovery + fresh initial configuration read.
  void establish() {
    reset_link_state_();
    parent_->current = esp32_ble_tracker::ClientState::ESTABLISHED;
    parent_->enabled = true;
    link_connected_ = transport_ready_ = config_fresh_ = true;
    disconnect_reported_ = preserve_disconnect_status_ = false;
    tx_handle_ = 1;
    rx_handle_ = 2;
    last_status_poll_ = millis();
    publish_connection_();
  }
};

struct Fixture {
  ChargerEventBus bus;
  ble_client::BLEClient client;
  TestCharger charger;
  std::vector<Event> events;
  bool fill_on_next_request{false};
  Fixture() {
    fake_millis = 0;
    native_writes.clear();
    native_write_result = ESP_OK;
    assert(bus.subscribe([this](const Event &event) {
      events.push_back(event);
      // An independent producer can consume the queue's freed slot before the
      // BLE subscriber publishes its lifecycle/result event.
      if (fill_on_next_request && EventCore::is_request(event.type)) {
        fill_on_next_request = false;
        Event filler{};
        while (bus.publish(filler)) {}
      }
    }));
    charger.set_event_bus(&bus);
    charger.attach(&client);
    charger.setup();
    charger.establish();
    drain();
  }
  void drain() { for (unsigned i = 0; i < 12; ++i) bus.loop(); }
  void tick(uint32_t time) { fake_millis = time; charger.loop(); drain(); }
  uint32_t request(EventType type, float voltage = NAN, float current = NAN) {
    const auto id = bus.request(type, "test", voltage, current);
    assert(id); drain(); return id;
  }
  void notify(uint8_t command, const std::vector<uint8_t> &payload) {
    uint8_t frame[protocol::MaxFrameSize];
    const auto size = protocol::encode(command, payload.data(), payload.size(), frame);
    esp_ble_gattc_cb_param_t param{};
    param.notify = {2, 1, static_cast<uint16_t>(size), frame};
    charger.gattc_event_handler(ESP_GATTC_NOTIFY_EVT, 1, &param);
    drain();
  }
  void status() { notify(0x84, std::vector<uint8_t>(15, 0)); }
  void config(uint16_t v = 584, uint16_t a = 51, uint8_t command = 0x82) {
    notify(command, {1, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v),
                    static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(a)});
  }
  void disconnect() {
    client.current = esp32_ble_tracker::ClientState::IDLE;
    esp_ble_gattc_cb_param_t param{};
    charger.gattc_event_handler(ESP_GATTC_DISCONNECT_EVT, 1, &param);
    charger.gattc_event_handler(ESP_GATTC_CLOSE_EVT, 1, &param);
    drain();
  }
  bool result(uint32_t id, Result value) const {
    for (const auto &event : events)
      if (event.type == EventType::STATUS && event.request_id == id && event.result == value) return true;
    return false;
  }
  std::vector<uint8_t> commands() const {
    std::vector<uint8_t> commands;
    for (const auto &frame : native_writes) commands.push_back(frame[3]);
    return commands;
  }
};

int main() {
  // Every permitted tenth, including the two-byte endpoints, traverses real BLE
  // validation, one write, matching echo and independent readback on a fake link.
  for (uint16_t raw = 500; raw <= 930; ++raw) {
    Fixture f;
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, raw / 10.0f, 5.1f);
    assert(f.result(id, Result::ACCEPTED));
    assert(f.commands() == std::vector<uint8_t>{3});
    assert(native_writes[0][5] == (raw >> 8) && native_writes[0][6] == (raw & 255));
    f.config(raw, 51, 0x83); f.tick(100); f.config(raw, 51);
    assert(f.result(id, Result::VERIFIED));
    assert((f.commands() == std::vector<uint8_t>{3, 2}));
    assert(f.bus.snapshot().voltage == raw / 10.0f);
  }
  for (float value : {49.9f, 93.1f, 50.05f, 92.95f, NAN, INFINITY}) {
    Fixture f; const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, value, 5.1f);
    assert(f.result(id, Result::REJECTED) && native_writes.empty());
  }

  { // Independent captured restart fixtures: output differs from 58.4 V setpoint.
    Fixture f;
    for (const auto raw : {0u, 1u, 259u, 587u, 588u, 589u, 590u}) {
      std::vector<uint8_t> data(15, 0);
      data[3] = raw >> 8; data[4] = raw & 255;
      data[10] = 32; data[11] = 34; data[12] = raw >= 587;
      f.tick(1000); f.notify(0x84, data);
      assert(f.bus.snapshot().output_voltage == raw / 10.0f);
      assert(std::isnan(f.bus.snapshot().output_current));
      assert(f.bus.snapshot().telemetry_inferred && f.bus.snapshot().telemetry_channels == 1);
    }
    f.notify(0x84, std::vector<uint8_t>(14, 0));  // Exact status layout required.
    assert(!f.bus.snapshot().telemetry_valid && std::isnan(f.bus.snapshot().output_voltage));
    std::vector<uint8_t> data(15, 0); data[3] = 0xff; data[4] = 0xff;
    f.notify(0x84, data);  // Out of this variant's decoder plausibility envelope.
    assert(!f.bus.snapshot().telemetry_valid);
    uint8_t captured[] = {0x5e,0x5e,0x11,0x84,0,0,0,2,0x4c,0,0,0,0,0,0x20,0x22,1,0,0,0xd8};
    float voltage = NAN;
    assert(protocol::decodeStatusVoltage(captured, sizeof(captured), voltage) && voltage == 58.8f);
    captured[8] ^= 1;
    assert(!protocol::decodeStatusVoltage(captured, sizeof(captured), voltage));
    assert(!protocol::decodeStatusVoltage(captured, 19, voltage));
  }

  { // Setup advertises inferred voltage only; enabling BLE is not a physical link.
    fake_millis = 0;
    native_writes.clear();
    native_write_result = ESP_OK;
    ChargerEventBus bus;
    ble_client::BLEClient client;
    client.current = esp32_ble_tracker::ClientState::IDLE;
    client.enabled = false;
    TestCharger charger;
    std::vector<Event> events;
    assert(bus.subscribe([&](const Event &event) { events.push_back(event); }));
    charger.set_event_bus(&bus);
    charger.attach(&client);
    charger.setup();
    for (unsigned i = 0; i < 8; ++i) bus.loop();
    assert(!charger.is_failed());
    assert(!bus.snapshot().connected && !bus.snapshot().connection_enabled);
    assert(bus.snapshot().telemetry_supported && !bus.snapshot().telemetry_valid);
    assert(bus.snapshot().telemetry_channels == 1 && bus.snapshot().telemetry_inferred);
    unsigned capabilities = 0;
    for (const auto &event : events) {
      if (event.type == EventType::TELEMETRY_CAPABILITY) {
        ++capabilities;
        assert(event.source == "ble" && event.telemetry_supported);
        assert(event.telemetry_channels == 1 && event.telemetry_inferred);
      }
      assert(event.type != EventType::TELEMETRY);
    }
    assert(capabilities == 1 && native_writes.empty());
    assert(bus.request(EventType::REQUEST_CONNECT, "test") != 0);
    for (unsigned i = 0; i < 8; ++i) bus.loop();
    assert(client.enabled && bus.snapshot().connection_enabled);
    assert(!bus.snapshot().connected && !bus.snapshot().ready);
    assert(bus.snapshot().output_state(0) == OutputState::DISCONNECTED);
    assert(native_writes.empty());
    charger.establish();
    for (unsigned i = 0; i < 8; ++i) bus.loop();
    assert(bus.snapshot().connected && bus.snapshot().ready);
    assert(bus.snapshot().output_state(0) == OutputState::WAITING);
    assert(bus.request(EventType::REQUEST_DISCONNECT, "test") != 0);
    for (unsigned i = 0; i < 8; ++i) bus.loop();
    assert(!client.enabled && !bus.snapshot().connection_enabled && !bus.snapshot().connected);
    assert(native_writes.empty());
  }
  { // A status sample retains its RX time and publishes voltage only, never a guessed current.
    Fixture f;
    assert(f.bus.snapshot().output_state(0) == OutputState::WAITING);
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_READ_CONFIG);
    f.tick(2120);
    f.status();
    assert(f.bus.snapshot().raw_status_seen && f.bus.snapshot().raw_sampled_at == 2120);
    assert(f.bus.snapshot().telemetry_valid && f.bus.snapshot().telemetry_seen);
    assert(f.bus.snapshot().output_voltage == 0.0f && std::isnan(f.bus.snapshot().output_current));
    assert(f.bus.snapshot().output_state(2120) == OutputState::LIVE);
    assert(f.commands() == std::vector<uint8_t>{4});
    f.tick(2320);
    assert((f.commands() == std::vector<uint8_t>{4, 2}));
    f.config();
    assert(f.result(id, Result::VERIFIED));
    assert(f.bus.snapshot().raw_sampled_at == 2120);
    unsigned raw_samples = 0;
    for (const auto &event : f.events) {
      if (event.type == EventType::TELEMETRY) {
        assert(event.telemetry_valid && event.output_voltage == 0.0f && std::isnan(event.output_current));
      }
      if (event.type == EventType::RAW_STATUS) {
        ++raw_samples;
        assert(event.source == "ble" && event.sampled_at == 2120);
      }
    }
    assert(raw_samples == 1);
    f.disconnect();
    assert(!f.bus.snapshot().raw_status_seen && !f.bus.snapshot().telemetry_seen);
    assert(f.bus.snapshot().connection_enabled);  // Unexpected link loss keeps reconnect intent.
    f.charger.establish(); f.drain();
    assert(!f.bus.snapshot().raw_status_seen);
    assert(f.bus.snapshot().output_state(2400) == OutputState::WAITING);
  }
  { // Reproduce the observed 04 / (120 ms) READ / 84 sequence.
    Fixture f;
    f.tick(2000);
    assert(!f.bus.snapshot().busy);  // Local draft edits remain available during polling.
    f.tick(2120);
    const auto id = f.request(EventType::REQUEST_READ_CONFIG);
    assert(f.result(id, Result::ACCEPTED) && f.bus.snapshot().busy);
    f.tick(2250);
    assert(f.commands() == std::vector<uint8_t>{4});
    f.status();
    assert(f.commands() == std::vector<uint8_t>{4}); // Never write inside RX callback.
    f.tick(2433);
    assert((f.commands() == std::vector<uint8_t>{4, 2}));
    f.tick(2450);
    f.config();
    assert(f.result(id, Result::VERIFIED) && !f.bus.snapshot().busy);
  }
  { // Freeze queued V/A + ID, reject another request, and send 03 exactly once.
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    const auto rejected = f.request(EventType::REQUEST_APPLY_CONFIG, 58.3f, 5.1f);
    assert(f.result(rejected, Result::REJECTED));
    f.config(); // A different command cannot release the status wait.
    f.tick(2200);
    assert(f.commands() == std::vector<uint8_t>{4});
    f.status();
    f.status(); // Duplicate status notifications cannot replay Apply.
    f.tick(2400);
    assert((f.commands() == std::vector<uint8_t>{4, 3}));
    assert(native_writes.back() == (std::vector<uint8_t>{0x5e,0x5e,7,3,1,2,0x48,0,0x32,0x7d}));
    for (unsigned i = 0; i < 20; ++i) f.tick(2450 + i);
    assert(f.commands().size() == 2);
    f.config(584, 50, 0x83);
    assert(f.commands().size() == 2);
    f.tick(2500);
    assert((f.commands() == std::vector<uint8_t>{4, 3, 2}));
    f.config(584, 50);
    assert(f.result(id, Result::VERIFIED));
    f.tick(2600);
    assert(f.commands().size() == 3);
  }
  for (auto type : {EventType::REQUEST_READ_CONFIG, EventType::REQUEST_APPLY_CONFIG}) {
    Fixture f;
    f.tick(2000);
    const auto id = f.request(type, 58.4f, 5.0f);
    f.tick(6999);
    assert(f.commands() == std::vector<uint8_t>{4});
    f.tick(7000);
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    assert(!f.bus.snapshot().ready && !f.bus.snapshot().busy);
    f.charger.establish(); f.drain();
    f.status(); f.tick(7100);
    assert(f.commands() == std::vector<uint8_t>{4}); // No replay across reconnect.
  }
  { // A late 84 before loop() also cannot revive an expired request.
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    fake_millis = 7000;
    f.status(); f.tick(7001);
    assert(f.result(id, Result::FAILED));
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  for (bool fallback : {false, true}) {
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    if (fallback) { f.client.current = esp32_ble_tracker::ClientState::IDLE; f.tick(2200); }
    else f.disconnect();
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    f.charger.establish(); f.drain(); f.status(); f.tick(2300);
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  { // Overflow while reporting disconnect still means the deferred request never sent.
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    Event filler{};
    while (f.bus.publish(filler)) {}
    f.disconnect();
    f.tick(2300);
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  { // Timeout must retain FAILED across reset even if its first notice overflows.
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    Event filler{};
    while (f.bus.publish(filler)) {}
    f.tick(7000); f.tick(7001);
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    f.charger.establish(); f.drain(); f.status(); f.tick(7100);
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  { // Losing the initial queued acceptance must not send or forget the request.
    Fixture f;
    f.tick(2000);
    f.fill_on_next_request = true;
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    f.tick(2200);
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  for (bool deferred : {true, false}) {
    Fixture f;
    if (deferred) f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    f.fill_on_next_request = true;
    const auto rejected = f.request(EventType::REQUEST_READ_CONFIG);
    f.tick(deferred ? 2200 : 200);
    // Failed publication of another request's rejection must not steal the
    // active Apply's correlation ID, whether it has been sent yet or not.
    assert(f.result(id, deferred ? Result::FAILED : Result::UNKNOWN));
    assert(f.result(rejected, Result::REJECTED));
    f.charger.establish(); f.drain(); f.status();
    f.tick(deferred ? 2300 : 300);
    assert(f.commands() == std::vector<uint8_t>{static_cast<uint8_t>(deferred ? 4 : 3)});
  }
  { // Asynchronous failure of the preceding 04 cannot mark an unsent Apply unknown.
    Fixture f;
    f.tick(2000);
    const auto id = f.request(EventType::REQUEST_APPLY_CONFIG, 58.4f, 5.0f);
    esp_ble_gattc_cb_param_t param{};
    param.write = {1, 1, 9};
    f.charger.gattc_event_handler(ESP_GATTC_WRITE_CHAR_EVT, 1, &param);
    f.drain();
    assert(f.result(id, Result::FAILED) && !f.result(id, Result::UNKNOWN));
    assert(f.commands() == std::vector<uint8_t>{4});
  }
  { // Poll timeout without a user request is bounded and cannot issue another 04.
    Fixture f;
    f.tick(2000); f.tick(4000); f.tick(6000); f.tick(7000);
    assert(f.commands() == std::vector<uint8_t>{4});
    assert(!f.bus.snapshot().ready);
  }
  puts("PASS: real BLE setup capability/connection intent/raw timestamps, 04/84 serialization, immutable Apply, single-send echo/readback, timeout/disconnect/overflow cancellation, no replay");
}
