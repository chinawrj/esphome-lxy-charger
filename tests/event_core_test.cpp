#include "event_core.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace esphome::charger_event_bus;

namespace {

Event connection(bool connected, bool ready, bool busy = false) {
  Event event;
  event.type = EventType::CONNECTION;
  event.connected = connected;
  event.ready = ready;
  event.busy = busy;
  return event;
}

Event config(float voltage, float current, uint32_t request_id = 0) {
  Event event;
  event.type = EventType::CONFIG;
  event.voltage = voltage;
  event.current = current;
  event.request_id = request_id;
  return event;
}

Event status(Result result, uint32_t request_id, bool busy, const char *message) {
  Event event;
  event.type = EventType::STATUS;
  event.result = result;
  event.request_id = request_id;
  event.busy = busy;
  event.message = message;
  return event;
}

void drain(EventCore &bus) {
  size_t rounds = 0;
  while (bus.pending()) {
    assert(++rounds < 100);
    assert(bus.dispatch() > 0);
  }
}

void test_fifo_overflow_and_deferred_state() {
  EventCore bus;
  std::vector<std::string> delivered;
  assert(bus.subscribe([&](const Event &event) { delivered.push_back(event.message); }));
  for (size_t i = 0; i < EventCore::QUEUE_CAPACITY; ++i) {
    Event event;
    event.type = EventType::INPUT;
    event.message = std::to_string(i);
    assert(bus.publish(event));
    event.message = "modified producer buffer";  // Queue owns a value copy.
  }
  assert(delivered.empty());
  assert(!bus.publish(connection(true, true)));
  assert(bus.request(EventType::REQUEST_READ_CONFIG, "full") == 0);
  assert(bus.dropped_events() == 2);
  assert(!bus.snapshot().connected);
  assert(bus.dispatch(3) == 3);
  assert(bus.pending() == EventCore::QUEUE_CAPACITY - 3);
  const uint32_t id = bus.request(EventType::REQUEST_READ_CONFIG, "after-overflow");
  assert(id == 1);  // Failed enqueue does not consume an allocated ID.
  drain(bus);
  assert(delivered.size() == EventCore::QUEUE_CAPACITY + 1);
  for (size_t i = 0; i < EventCore::QUEUE_CAPACITY; ++i) assert(delivered[i] == std::to_string(i));
  assert(delivered.back().empty());
  assert(bus.publish(connection(true, false)));
  assert(!bus.snapshot().connected);  // State changes only during event dispatch.
  drain(bus);
  assert(bus.snapshot().connected && !bus.snapshot().ready);
}

void test_request_validation_and_id_exhaustion() {
  EventCore bus;
  std::vector<Event> requests;
  assert(bus.subscribe([&](const Event &event) { requests.push_back(event); }));
  for (EventType type : {EventType::CONNECTION, EventType::CONFIG, EventType::STATUS,
                        EventType::RAW_STATUS, EventType::INPUT, EventType::NETWORK_STATE,
                        static_cast<EventType>(255)}) {
    assert(bus.request(type, "invalid") == 0);
  }
  assert(bus.pending() == 0 && requests.empty());
  uint32_t expected_id = 1;
  for (EventType type : {EventType::REQUEST_CONNECT, EventType::REQUEST_DISCONNECT,
                        EventType::REQUEST_READ_CONFIG, EventType::REQUEST_APPLY_CONFIG}) {
    assert(bus.request(type, "producer", 58.4f, 5.0f) == expected_id++);
  }
  assert(requests.empty());
  drain(bus);
  assert(requests.size() == 4);
  for (size_t i = 0; i < requests.size(); ++i) {
    assert(requests[i].request_id == i + 1);
    assert(requests[i].source == "producer");
    assert(requests[i].voltage == 58.4f && requests[i].current == 5.0f);
  }
  // The bus routes numeric payloads; the BLE module is their validator.
  assert(bus.request(EventType::REQUEST_APPLY_CONFIG, "unvalidated", NAN, INFINITY) == 5);
  drain(bus);
  assert(std::isnan(requests.back().voltage) && std::isinf(requests.back().current));

  EventCore end(std::numeric_limits<uint32_t>::max() - 1);
  assert(end.request(EventType::REQUEST_CONNECT, "last-two") == std::numeric_limits<uint32_t>::max() - 1);
  assert(end.request(EventType::REQUEST_CONNECT, "last") == std::numeric_limits<uint32_t>::max());
  assert(end.request(EventType::REQUEST_CONNECT, "exhausted") == 0);
  drain(end);
  assert(end.request(EventType::REQUEST_CONNECT, "still-exhausted") == 0);
}

void test_non_reentrant_bounded_dispatch() {
  EventCore bus;
  std::vector<std::string> order;
  int callback_depth = 0;
  int max_depth = 0;
  assert(bus.subscribe([&](const Event &event) {
    ++callback_depth;
    if (callback_depth > max_depth) max_depth = callback_depth;
    order.push_back("A:" + event.message);
    if (event.message == "start") {
      Event followup;
      followup.type = EventType::INPUT;
      followup.message = "followup";
      assert(bus.publish(followup));
      assert(bus.dispatch(100) == 0);
      assert(!bus.subscribe([](const Event &) {}));
    }
    --callback_depth;
  }));
  assert(bus.subscribe([&](const Event &event) { order.push_back("B:" + event.message); }));
  Event start;
  start.type = EventType::INPUT;
  start.message = "start";
  assert(bus.publish(start));
  start.message = "already-queued";
  assert(bus.publish(start));
  assert(bus.dispatch(0) == 0 && order.empty());
  assert(bus.dispatch(1) == 1);
  assert((order == std::vector<std::string>{"A:start", "B:start"}));
  assert(bus.pending() == 2);
  drain(bus);
  assert((order == std::vector<std::string>{"A:start", "B:start", "A:already-queued", "B:already-queued",
                                          "A:followup", "B:followup"}));
  assert(max_depth == 1);

  EventCore feedback;
  size_t calls = 0;
  assert(feedback.subscribe([&](const Event &event) {
    ++calls;
    assert(feedback.publish(event));
  }));
  assert(feedback.publish(start));
  assert(feedback.dispatch() == EventCore::DEFAULT_DISPATCH_BUDGET);
  assert(calls == EventCore::DEFAULT_DISPATCH_BUDGET && feedback.pending() == 1);

  EventCore listeners;
  assert(!listeners.subscribe({}));
  for (size_t i = 0; i < EventCore::SUBSCRIBER_CAPACITY; ++i) assert(listeners.subscribe([](const Event &) {}));
  assert(!listeners.subscribe([](const Event &) {}));
}

void test_snapshot_lifecycle_and_correlation() {
  EventCore bus;
  assert(!bus.snapshot().connected && !bus.snapshot().ready && !bus.snapshot().busy);
  assert(std::isnan(bus.snapshot().voltage) && std::isnan(bus.snapshot().current));
  assert(bus.publish(config(58.4f, 5.1f, 77)));
  drain(bus);
  assert(std::isnan(bus.snapshot().voltage));  // Stale/offline CONFIG ignored.

  bool observer_saw_updated_snapshot = false;
  assert(bus.subscribe([&](const Event &event) {
    if (event.type == EventType::CONFIG && bus.snapshot().connected) {
      assert(bus.snapshot().voltage == event.voltage);
      assert(bus.snapshot().last_request_id == event.request_id);
      observer_saw_updated_snapshot = true;
    }
  }));
  assert(bus.publish(connection(true, false)));
  assert(bus.publish(config(58.4f, 5.1f, 11)));
  drain(bus);
  assert(observer_saw_updated_snapshot);
  assert(!bus.snapshot().ready);  // CONFIG alone never establishes readiness.
  assert(bus.snapshot().voltage == 58.4f && bus.snapshot().current == 5.1f);
  assert(bus.publish(connection(true, true)));
  assert(bus.publish(status(Result::ACCEPTED, 12, true, "pending")));
  drain(bus);
  assert(bus.snapshot().ready && bus.snapshot().busy && bus.snapshot().last_request_id == 12);
  assert(bus.snapshot().result == Result::ACCEPTED && bus.snapshot().status == "pending");
  // A local web message can be queued behind a BLE transition, using an old
  // UI snapshot. INPUT must not overwrite BLE busy/result/correlation even
  // when its fields contain that stale snapshot's values.
  assert(bus.publish(status(Result::ACCEPTED, 13, true, "new BLE operation")));
  Event web_feedback;
  web_feedback.type = EventType::INPUT;
  web_feedback.source = "web";
  web_feedback.request_id = 0;
  web_feedback.busy = false;
  web_feedback.result = Result::REJECTED;
  web_feedback.message = "local draft feedback";
  assert(bus.publish(web_feedback));
  drain(bus);
  assert(bus.snapshot().busy && bus.snapshot().last_request_id == 13);
  assert(bus.snapshot().result == Result::ACCEPTED && bus.snapshot().status == "new BLE operation");
  assert(bus.publish(config(58.3f, 5.0f, 12)));
  assert(bus.publish(status(Result::VERIFIED, 12, false, "verified")));
  Event raw;
  raw.type = EventType::RAW_STATUS;
  raw.message = "5E5E1184...";
  assert(bus.publish(raw));
  Event network;
  network.type = EventType::NETWORK_STATE;
  network.connected = true;
  network.message = "192.168.4.1";
  assert(bus.publish(network));
  drain(bus);
  assert(bus.snapshot().voltage == 58.3f && bus.snapshot().current == 5.0f);
  assert(bus.snapshot().result == Result::VERIFIED && !bus.snapshot().busy);
  assert(bus.snapshot().raw_status == raw.message && bus.snapshot().ip_address == network.message);

  assert(bus.publish(connection(true, false)));
  drain(bus);
  assert(std::isnan(bus.snapshot().voltage) && std::isnan(bus.snapshot().current));
  assert(bus.publish(connection(false, true, true)));  // Normalize impossible readiness.
  assert(bus.publish(config(58.4f, 5.1f, 99)));
  assert(bus.publish(status(Result::ACCEPTED, 99, true, "late status")));
  network.connected = false;
  assert(bus.publish(network));
  drain(bus);
  assert(!bus.snapshot().connected && !bus.snapshot().ready && !bus.snapshot().busy);
  assert(std::isnan(bus.snapshot().voltage) && std::isnan(bus.snapshot().current));
  assert(bus.snapshot().ip_address.empty());
  const Snapshot before_input = bus.snapshot();
  Event input;
  input.type = EventType::INPUT;
  input.message = "button B";
  assert(bus.publish(input));
  assert(bus.request(EventType::REQUEST_CONNECT, "button") != 0);
  drain(bus);
  assert(bus.snapshot().status == before_input.status && bus.snapshot().result == before_input.result);
  assert(bus.snapshot().last_request_id == before_input.last_request_id && !bus.snapshot().connected);
}

// These mocks model independent module boundaries, not BLE wire behavior.
// Mandatory BLE records requests; test-produced response events exercise the
// bus. Optional observers/producers have no references to the BLE recorder.
struct MockLCD {
  unsigned renders{0};
  bool showing_values{false};
  void observe(const Event &, const Snapshot &state) {
    ++renders;
    showing_values = state.ready && std::isfinite(state.voltage) && std::isfinite(state.current);
  }
};

struct MockLED {
  bool on{false};
  unsigned updates{0};
  void observe(const Event &event, const Snapshot &state) {
    if (event.type == EventType::CONNECTION) { on = state.ready; ++updates; }
  }
};

struct MockButton {
  uint32_t press_a(EventCore &bus) { return bus.request(EventType::REQUEST_READ_CONFIG, "button_a"); }
  void press_b(EventCore &bus) {
    Event event;
    event.type = EventType::INPUT;
    event.source = "button_b";
    event.message = "pressed";
    assert(bus.publish(event));
  }
};

struct MockWeb {
  float draft_voltage{NAN};
  float draft_current{NAN};
  unsigned observed{0};
  void observe(const Event &event) {
    ++observed;
    if (event.type == EventType::CONFIG && !std::isfinite(draft_voltage)) {
      draft_voltage = event.voltage;
      draft_current = event.current;
    }
  }
  void stage(float voltage, float current) { draft_voltage = voltage; draft_current = current; }
  uint32_t apply(EventCore &bus) {
    return bus.request(EventType::REQUEST_APPLY_CONFIG, "web", draft_voltage, draft_current);
  }
};

void test_all_optional_profiles() {
  for (unsigned mask = 0; mask < 16; ++mask) {
    const bool lcd_enabled = mask & 1;
    const bool button_enabled = mask & 2;
    const bool led_enabled = mask & 4;
    const bool web_enabled = mask & 8;
    EventCore bus;
    std::vector<Event> ble_requests;
    assert(bus.subscribe([&](const Event &event) {
      if (EventCore::is_request(event.type)) ble_requests.push_back(event);
    }));
    MockLCD lcd;
    MockButton button;
    MockLED led;
    MockWeb web;
    if (lcd_enabled) assert(bus.subscribe([&](const Event &event) { lcd.observe(event, bus.snapshot()); }));
    if (led_enabled) assert(bus.subscribe([&](const Event &event) { led.observe(event, bus.snapshot()); }));
    if (web_enabled) assert(bus.subscribe([&](const Event &event) { web.observe(event); }));
    assert(bus.publish(connection(true, false)));
    assert(bus.publish(config(58.4f, 5.1f)));
    assert(bus.publish(connection(true, true)));
    drain(bus);
    assert(ble_requests.empty());  // No observer initiates a setting on boot.
    assert(bus.snapshot().ready);
    assert(lcd.showing_values == lcd_enabled && led.on == led_enabled);
    assert((lcd.renders > 0) == lcd_enabled && (led.updates > 0) == led_enabled);
    assert((web.observed > 0) == web_enabled);
    std::vector<uint32_t> ids;
    if (button_enabled) {
      ids.push_back(button.press_a(bus));
      button.press_b(bus);
    }
    if (web_enabled) {
      const size_t before = bus.pending();
      web.stage(58.3f, 5.0f);
      assert(bus.pending() == before);  // Draft edit is local, never BLE traffic.
      ids.push_back(web.apply(bus));
    }
    assert(ble_requests.empty());  // Producers never call BLE synchronously.
    drain(bus);
    assert(ble_requests.size() == unsigned(button_enabled) + unsigned(web_enabled));
    for (size_t i = 0; i < ids.size(); ++i) {
      assert(ids[i] == i + 1 && ble_requests[i].request_id == ids[i]);
    }
    size_t index = 0;
    if (button_enabled) {
      assert(ble_requests[index].type == EventType::REQUEST_READ_CONFIG);
      assert(ble_requests[index++].source == "button_a");
    }
    if (web_enabled) {
      assert(ble_requests[index].type == EventType::REQUEST_APPLY_CONFIG);
      assert(ble_requests[index].source == "web");
      assert(ble_requests[index].voltage == 58.3f && ble_requests[index].current == 5.0f);
    }
    for (uint32_t id : ids) {
      assert(bus.publish(status(Result::ACCEPTED, id, true, "accepted")));
      assert(bus.publish(config(58.3f, 5.0f, id)));
      assert(bus.publish(status(Result::VERIFIED, id, false, "verified")));
      drain(bus);
      assert(bus.snapshot().last_request_id == id && bus.snapshot().result == Result::VERIFIED);
    }
    const size_t requests_before_disconnect = ble_requests.size();
    assert(bus.publish(connection(false, false)));
    drain(bus);
    assert(!bus.snapshot().ready && std::isnan(bus.snapshot().voltage));
    assert(!lcd.showing_values && !led.on);
    assert(bus.publish(connection(true, false)));
    assert(bus.publish(config(58.4f, 5.1f)));
    assert(bus.publish(connection(true, true)));
    drain(bus);
    assert(ble_requests.size() == requests_before_disconnect);  // No replay on reconnect.
    if (web_enabled) assert(web.draft_voltage == 58.3f && web.draft_current == 5.0f);
    std::printf("PASS profile %02u: BLE=1 LCD=%u Button=%u LED=%u Web=%u\n", mask,
                unsigned(lcd_enabled), unsigned(button_enabled), unsigned(led_enabled), unsigned(web_enabled));
  }
}

}  // namespace

int main() {
  test_fifo_overflow_and_deferred_state();
  test_request_validation_and_id_exhaustion();
  test_non_reentrant_bounded_dispatch();
  test_snapshot_lifecycle_and_correlation();
  test_all_optional_profiles();
  std::puts("PASS: queue overflow/FIFO, invalid requests, IDs/exhaustion, reentrancy/budget, snapshot/correlation, 16 profiles");
}
