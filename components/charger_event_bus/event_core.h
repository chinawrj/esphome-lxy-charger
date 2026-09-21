#pragma once

// No ESPHome, ESP-IDF, Arduino, radio, or entity dependencies. All producers
// call this core from the application's main loop; it is not thread-safe.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace esphome::charger_event_bus {

enum class EventType {
  REQUEST_CONNECT, REQUEST_DISCONNECT, REQUEST_READ_CONFIG, REQUEST_APPLY_CONFIG,
  CONNECTION, CONFIG, STATUS, RAW_STATUS, INPUT, NETWORK_STATE
};

enum class Result { INFO, ACCEPTED, VERIFIED, REJECTED, FAILED, UNKNOWN };

struct Event {
  EventType type{EventType::STATUS};
  uint32_t request_id{0};
  bool connected{false};
  bool ready{false};
  bool busy{false};
  float voltage{NAN};
  float current{NAN};
  Result result{Result::INFO};
  std::string message;
  std::string source;
};

struct Snapshot {
  bool connected{false};
  bool ready{false};
  bool busy{false};
  float voltage{NAN};
  float current{NAN};
  std::string status;
  std::string raw_status;
  std::string ip_address;
  uint32_t last_request_id{0};
  Result result{Result::INFO};
};

class EventCore {
 public:
  static constexpr size_t QUEUE_CAPACITY = 32;
  static constexpr size_t SUBSCRIBER_CAPACITY = 16;
  static constexpr size_t DEFAULT_DISPATCH_BUDGET = 8;
  using Callback = std::function<void(const Event &)>;

  // A non-default first ID is useful for deterministic native exhaustion
  // tests. Zero means exhausted; IDs never wrap or get reused within a boot.
  explicit EventCore(uint32_t first_request_id = 1) : next_request_id_(first_request_id) {}

  static bool is_request(EventType type) {
    return type == EventType::REQUEST_CONNECT || type == EventType::REQUEST_DISCONNECT ||
           type == EventType::REQUEST_READ_CONFIG || type == EventType::REQUEST_APPLY_CONFIG;
  }

  bool publish(const Event &event) {
    if (this->queue_size_ == QUEUE_CAPACITY) {
      if (this->dropped_events_ != std::numeric_limits<uint32_t>::max()) ++this->dropped_events_;
      return false;
    }
    this->queue_[(this->queue_head_ + this->queue_size_) % QUEUE_CAPACITY] = event;
    ++this->queue_size_;
    return true;
  }

  // Registration is a setup operation. Keeping the listener set fixed during
  // dispatch prevents callbacks from changing the current delivery list.
  bool subscribe(Callback callback) {
    if (!callback || this->dispatching_ || this->subscriber_count_ == SUBSCRIBER_CAPACITY) return false;
    this->subscribers_[this->subscriber_count_++] = std::move(callback);
    return true;
  }

  uint32_t request(EventType type, const std::string &source, float voltage = NAN, float current = NAN) {
    if (!is_request(type) || this->next_request_id_ == 0) return 0;
    Event event;
    event.type = type;
    event.request_id = this->next_request_id_;
    event.source = source;
    event.voltage = voltage;
    event.current = current;
    if (!this->publish(event)) return 0;
    // Allocation advances only after successful enqueue, including UINT32_MAX.
    this->next_request_id_ = this->next_request_id_ == std::numeric_limits<uint32_t>::max()
                                 ? 0 : this->next_request_id_ + 1;
    return event.request_id;
  }

  size_t dispatch(size_t budget = DEFAULT_DISPATCH_BUDGET) {
    if (this->dispatching_ || budget == 0) return 0;
    this->dispatching_ = true;
    struct DispatchGuard {
      bool &active;
      ~DispatchGuard() { active = false; }
    } guard{this->dispatching_};

    size_t delivered = 0;
    while (this->queue_size_ && delivered < budget) {
      // Pop before delivery so callbacks can append safely even when full.
      Event event = std::move(this->queue_[this->queue_head_]);
      this->queue_head_ = (this->queue_head_ + 1) % QUEUE_CAPACITY;
      --this->queue_size_;
      this->reduce_(event);
      for (size_t i = 0; i < this->subscriber_count_; ++i) this->subscribers_[i](event);
      ++delivered;
    }
    return delivered;
  }

  const Snapshot &snapshot() const { return this->snapshot_; }
  size_t pending() const { return this->queue_size_; }
  size_t subscriber_count() const { return this->subscriber_count_; }
  uint32_t dropped_events() const { return this->dropped_events_; }

 private:
  void reduce_(const Event &event) {
    switch (event.type) {
      case EventType::CONNECTION:
        this->snapshot_.connected = event.connected;
        this->snapshot_.ready = event.connected && event.ready;
        this->snapshot_.busy = event.connected && event.busy;
        if (!this->snapshot_.ready) this->snapshot_.voltage = this->snapshot_.current = NAN;
        break;
      case EventType::CONFIG:
        // A delayed configuration event cannot resurrect readings on a lost
        // connection. A fresh connection must be announced before its CONFIG.
        if (!this->snapshot_.connected) break;
        this->snapshot_.voltage = event.voltage;
        this->snapshot_.current = event.current;
        this->snapshot_.last_request_id = event.request_id;
        break;
      case EventType::STATUS:
        this->snapshot_.status = event.message;
        this->snapshot_.result = event.result;
        this->snapshot_.last_request_id = event.request_id;
        // Late ACCEPTED/status events cannot keep an offline device busy.
        this->snapshot_.busy = this->snapshot_.connected && event.busy;
        break;
      case EventType::RAW_STATUS:
        this->snapshot_.raw_status = event.message;
        break;
      case EventType::NETWORK_STATE:
        this->snapshot_.ip_address = event.connected ? event.message : std::string{};
        break;
      default:
        // Requests and physical-input events convey intent only. BLE owns
        // the actual transport/transaction state reported above.
        break;
    }
  }

  std::array<Event, QUEUE_CAPACITY> queue_{};
  std::array<Callback, SUBSCRIBER_CAPACITY> subscribers_{};
  Snapshot snapshot_{};
  size_t queue_head_{0};
  size_t queue_size_{0};
  size_t subscriber_count_{0};
  uint32_t next_request_id_{1};
  uint32_t dropped_events_{0};
  bool dispatching_{false};
};

}  // namespace esphome::charger_event_bus
