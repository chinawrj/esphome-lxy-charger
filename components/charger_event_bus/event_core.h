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
  CONNECTION, CONFIG, STATUS, RAW_STATUS, INPUT, NETWORK_STATE,
  TELEMETRY, UI_DISPLAY, UI_STATE, UI_CONTROLS, TELEMETRY_CAPABILITY, BOARD_BATTERY
};

enum class Result { INFO, ACCEPTED, VERIFIED, REJECTED, FAILED, UNKNOWN };
enum class UiMode { VIEW, EDIT, CONFIRM, SUBMITTING, REFRESHING, CONNECTING, HELP, METER };
enum class UiField { VOLTAGE, CURRENT };
enum class UiNotice {
  NONE, SELECTED, EDITING, REVIEW, APPLY_PENDING, REFRESH_PENDING, APPLIED, REFRESHED,
  CONNECT_PENDING, CONNECTED, CONNECT_FAILED,
  CANCELLED, UNCHANGED, LIMIT, DISPLAY_UNAVAILABLE, NOT_READY, BUSY, CONFIG_CHANGED,
  TIMED_OUT, INPUT_INTERRUPTED, QUEUE_FULL, REJECTED, FAILED, UNKNOWN
};
// A hint to release the held key; crossing the threshold never sends a request.
enum class UiHold { NONE, EDIT, REVIEW, APPLY, CANCEL, RELEASE, HELP };
enum class OutputState { DISCONNECTED, INITIALIZING, UNSUPPORTED, WAITING, LIVE, STALE, INVALID };

struct Event {
  EventType type{EventType::STATUS};
  uint32_t request_id{0};
  bool connected{false};
  bool connection_enabled{false};
  bool ready{false};
  bool busy{false};
  float voltage{NAN};
  float current{NAN};
  // Measured output is separate from configuration, even when numerically equal.
  float output_voltage{NAN};
  float output_current{NAN};
  uint32_t sampled_at{0};
  bool telemetry_valid{false};
  bool telemetry_supported{false};
  uint8_t telemetry_channels{3};  // bit 0: voltage, bit 1: current
  bool telemetry_inferred{false};  // Mapping inferred from frames, not cross-checked.
  float battery_voltage{NAN};
  float battery_charge_ma{NAN};
  float battery_discharge_ma{NAN};
  bool battery_valid{false};
  bool battery_present{false};
  UiMode ui_mode{UiMode::VIEW};
  UiNotice ui_notice{UiNotice::NONE};
  UiHold ui_hold{UiHold::NONE};
  UiField ui_field{UiField::VOLTAGE};
  bool ui_display_ready{false};
  bool ui_buttons_ready{false};
  Result result{Result::INFO};
  std::string message;
  std::string source;
};

struct Snapshot {
  bool connected{false};
  bool connection_enabled{false};
  bool ready{false};
  bool busy{false};
  float voltage{NAN};
  float current{NAN};
  float output_voltage{NAN};
  float output_current{NAN};
  uint32_t sampled_at{0};
  bool telemetry_valid{false};
  bool telemetry_supported{false};
  uint8_t telemetry_channels{3};
  bool telemetry_inferred{false};
  bool ui_display_ready{false};
  bool ui_buttons_ready{false};
  float battery_voltage{NAN};
  float battery_charge_ma{NAN};
  float battery_discharge_ma{NAN};
  bool battery_valid{false};
  bool battery_present{false};
  UiMode ui_mode{UiMode::VIEW};
  UiNotice ui_notice{UiNotice::NONE};
  UiHold ui_hold{UiHold::NONE};
  UiField ui_field{UiField::VOLTAGE};
  float ui_voltage{NAN};
  float ui_current{NAN};
  uint32_t ui_request_id{0};
  uint32_t ui_updated_at{0};
  bool telemetry_seen{false};
  bool raw_status_seen{false};
  uint32_t raw_sampled_at{0};
  Result ui_result{Result::INFO};
  std::string ui_message;
  std::string status;
  std::string raw_status;
  std::string ip_address;
  uint32_t last_request_id{0};
  Result result{Result::INFO};

  bool battery_seen{false};
  uint32_t battery_sampled_at{0};
  float battery_current_ma{NAN};  // Positive charges this board's battery.
  bool battery_fresh(uint32_t now) const {
    return battery_seen && battery_valid && uint32_t(now - battery_sampled_at) < 6000;
  }

  OutputState output_state(uint32_t now) const {
    if (!connected) return OutputState::DISCONNECTED;
    if (telemetry_fresh(now)) return OutputState::LIVE;
    if (!ready) return OutputState::INITIALIZING;
    if (!telemetry_supported) return OutputState::UNSUPPORTED;
    if (!telemetry_seen) return OutputState::WAITING;
    return telemetry_valid ? OutputState::STALE : OutputState::INVALID;
  }

  bool telemetry_fresh(uint32_t now, uint32_t max_age_ms = 6000) const {
    return connected && telemetry_supported && telemetry_valid && uint32_t(now - sampled_at) < max_age_ms;
  }
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
        this->snapshot_.connection_enabled = event.connection_enabled;
        this->snapshot_.ready = event.connected && event.ready;
        this->snapshot_.busy = event.connected && event.busy;
        if (!this->snapshot_.ready) this->snapshot_.voltage = this->snapshot_.current = NAN;
        if (!this->snapshot_.connected) {
          this->snapshot_.telemetry_valid = false;
          this->snapshot_.telemetry_seen = this->snapshot_.raw_status_seen = false;
          this->snapshot_.output_voltage = this->snapshot_.output_current = NAN;
        }
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
        if (this->snapshot_.connected) {
          this->snapshot_.raw_status_seen = true;
          this->snapshot_.raw_sampled_at = event.sampled_at;
        }
        break;
      case EventType::NETWORK_STATE:
        this->snapshot_.ip_address = event.connected ? event.message : std::string{};
        break;
      case EventType::TELEMETRY_CAPABILITY:
        if (!event.telemetry_supported || this->snapshot_.telemetry_channels != (event.telemetry_channels & 3) ||
            this->snapshot_.telemetry_inferred != event.telemetry_inferred) {
          this->snapshot_.telemetry_valid = this->snapshot_.telemetry_seen = false;
          this->snapshot_.output_voltage = this->snapshot_.output_current = NAN;
        }
        this->snapshot_.telemetry_channels = event.telemetry_channels & 3;
        this->snapshot_.telemetry_supported = event.telemetry_supported && this->snapshot_.telemetry_channels != 0;
        this->snapshot_.telemetry_inferred = event.telemetry_inferred;
        break;
      case EventType::TELEMETRY:
        this->snapshot_.telemetry_seen = this->snapshot_.connected && this->snapshot_.telemetry_supported;
        this->snapshot_.telemetry_valid = this->snapshot_.connected && this->snapshot_.telemetry_supported && event.telemetry_valid &&
            (!(this->snapshot_.telemetry_channels & 1) || std::isfinite(event.output_voltage)) &&
            (!(this->snapshot_.telemetry_channels & 2) || std::isfinite(event.output_current));
        this->snapshot_.output_voltage = this->snapshot_.telemetry_valid && (this->snapshot_.telemetry_channels & 1) ? event.output_voltage : NAN;
        this->snapshot_.output_current = this->snapshot_.telemetry_valid && (this->snapshot_.telemetry_channels & 2) ? event.output_current : NAN;
        this->snapshot_.sampled_at = event.sampled_at;
        break;
      case EventType::BOARD_BATTERY: {
        auto &s = this->snapshot_;
        s.battery_seen = true;
        s.battery_sampled_at = event.sampled_at;
        s.battery_valid = event.battery_valid && (!event.battery_present ||
            (std::isfinite(event.battery_voltage) && event.battery_voltage >= 0 &&
             std::isfinite(event.battery_charge_ma) && event.battery_charge_ma >= 0 &&
             std::isfinite(event.battery_discharge_ma) && event.battery_discharge_ma >= 0));
        s.battery_present = s.battery_valid && event.battery_present;
        s.battery_voltage = s.battery_present ? event.battery_voltage : NAN;
        s.battery_charge_ma = s.battery_present ? event.battery_charge_ma : NAN;
        s.battery_discharge_ma = s.battery_present ? event.battery_discharge_ma : NAN;
        s.battery_current_ma = s.battery_charge_ma - s.battery_discharge_ma;
        break;
      }
      case EventType::UI_DISPLAY:
        this->snapshot_.ui_display_ready = event.ui_display_ready;
        break;
      case EventType::UI_CONTROLS:
        this->snapshot_.ui_buttons_ready = event.ui_buttons_ready;
        break;
      case EventType::UI_STATE:
        this->snapshot_.ui_mode = event.ui_mode;
        this->snapshot_.ui_notice = event.ui_notice;
        this->snapshot_.ui_hold = event.ui_hold;
        this->snapshot_.ui_updated_at = event.sampled_at;
        this->snapshot_.ui_field = event.ui_field;
        this->snapshot_.ui_voltage = event.voltage;
        this->snapshot_.ui_current = event.current;
        this->snapshot_.ui_request_id = event.request_id;
        this->snapshot_.ui_result = event.result;
        this->snapshot_.ui_message = event.message;
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
