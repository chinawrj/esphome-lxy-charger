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
  puts("PASS: real BLE 04/84 serialization, one queued request, immutable Apply, single-send echo/readback, timeout/disconnect/overflow cancellation, no replay");
}
