#include "charger_web.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome::charger_web {
using charger_event_bus::Event;
using charger_event_bus::EventType;
using charger_event_bus::Result;
static const char *const TAG = "charger_web";

void DraftNumber::control(float value) { owner_->stage(voltage_, value); }
void RequestButton::press_action() { if (apply_) owner_->apply(); else owner_->refresh(); }
void ConnectionSwitch::write_state(bool state) { owner_->connect(state); }

void ChargerWeb::setup() {
  if (bus_ == nullptr || !bus_->subscribe([this](const Event &event) { on_event_(event); })) {
    ESP_LOGE(TAG, "Cannot subscribe to event bus");
    this->mark_failed();
    return;
  }
  ready_sensor_->publish_state(false);
  voltage_sensor_->publish_state(NAN);
  current_sensor_->publish_state(NAN);
  if (output_voltage_sensor_) output_voltage_sensor_->publish_state(NAN);
  if (output_current_sensor_) output_current_sensor_->publish_state(NAN);
  if (telemetry_sensor_) telemetry_sensor_->publish_state(false);
}

void ChargerWeb::loop() {
  if (bus_ == nullptr) return;
  const auto &state = bus_->snapshot();
  const char *link = state.connected ? (state.ready ? "Connected (ready)" : "Connected (initializing)") :
      (state.connection_enabled ? "Connecting" : "Disconnected / disabled");
  if (link_status_sensor_ && published_link_status_ != link) {
    published_link_status_ = link;
    link_status_sensor_->publish_state(published_link_status_);
  }
  const char *output = "Waiting for BLE connection";
  using charger_event_bus::OutputState;
  switch (state.output_state(millis())) {
    case OutputState::INITIALIZING: output = "BLE connected; initializing"; break;
    case OutputState::UNSUPPORTED: output = "BLE connected; output decoding not implemented"; break;
    case OutputState::WAITING: output = "Waiting for first output sample"; break;
    case OutputState::LIVE:
      output = state.telemetry_inferred ? "Live voltage (inferred mapping); current unavailable" : "Live measured output";
      break;
    case OutputState::STALE: output = "Output sample expired"; break;
    case OutputState::INVALID: output = "Invalid output sample"; break;
    default: break;
  }
  if (output_status_sensor_ && published_output_status_ != output) {
    published_output_status_ = output;
    output_status_sensor_->publish_state(published_output_status_);
  }
  if (output_published_valid_ && !state.telemetry_fresh(millis())) {
    output_published_valid_ = false;
    if (output_voltage_sensor_) output_voltage_sensor_->publish_state(NAN);
    if (output_current_sensor_) output_current_sensor_->publish_state(NAN);
    if (telemetry_sensor_) telemetry_sensor_->publish_state(false);
  }
}

bool ChargerWeb::valid_(float value, bool voltage) {
  if (!std::isfinite(value)) return false;
  const float scaled = value * 10.0f;
  const float rounded = std::round(scaled);
  return std::fabs(scaled - rounded) <= 0.001f &&
      (voltage ? (rounded >= 500 && rounded <= 930) : (rounded >= 49 && rounded <= 51));
}

void ChargerWeb::status_(const char *message, Result result) {
  // UI feedback must not overwrite the BLE-owned transaction state/correlation.
  Event event{EventType::INPUT};
  event.source = "web";
  event.message = message;
  event.result = result;
  if (!bus_->publish(event)) {
    // A failed enqueue never implies a command reached the charger.
    status_sensor_->publish_state("Event queue full; command was not submitted");
    ESP_LOGW(TAG, "Event queue full");
  }
}

bool ChargerWeb::stage(bool voltage, float value) {
  if (bus_->snapshot().busy) {
    status_("Busy: wait for the current query or Apply to finish", Result::REJECTED);
    return false;
  }
  if (!valid_(value, voltage)) {
    status_("Rejected: use 0.1 steps within the configured limits", Result::REJECTED);
    return false;
  }
  value = std::round(value * 10.0f) / 10.0f;
  if (voltage) {
    voltage_ = value;
    voltage_number_->publish_state(value);
  } else {
    current_ = value;
    current_number_->publish_state(value);
  }
  status_("Draft updated; press Apply to send both values", Result::INFO);
  return true;
}

void ChargerWeb::apply() {
  const auto &state = bus_->snapshot();
  if (!state.ready || state.busy || !valid_(voltage_, true) || !valid_(current_, false)) {
    status_("Apply rejected: require ready, idle charger and two valid draft values", Result::REJECTED);
    return;
  }
  if (bus_->request(EventType::REQUEST_APPLY_CONFIG, "web", voltage_, current_) == 0)
    status_("Apply rejected: event queue full", Result::REJECTED);
}

void ChargerWeb::refresh() {
  if (bus_->request(EventType::REQUEST_READ_CONFIG, "web") == 0)
    status_("Refresh rejected: event queue full", Result::REJECTED);
}

void ChargerWeb::connect(bool enabled) {
  if (bus_->request(enabled ? EventType::REQUEST_CONNECT : EventType::REQUEST_DISCONNECT, "web") == 0) {
    status_("Connection request rejected: event queue full", Result::REJECTED);
    return;
  }
  // A queued request is not a connection result. Only CONNECTION events
  // publish the switch state, including when BLE rejects a busy request.
}

void ChargerWeb::on_event_(const Event &event) {
  switch (event.type) {
    case EventType::CONNECTION:
      ready_sensor_->publish_state(bus_->snapshot().ready);
      connection_switch_->publish_state(event.connected);
      if (!bus_->snapshot().ready) {
        voltage_sensor_->publish_state(NAN);
        current_sensor_->publish_state(NAN);
      }
      break;
    case EventType::CONFIG:
      if (!bus_->snapshot().connected) break;
      voltage_sensor_->publish_state(event.voltage);
      current_sensor_->publish_state(event.current);
      if (!std::isfinite(voltage_) && valid_(event.voltage, true)) {
        voltage_ = event.voltage;
        voltage_number_->publish_state(voltage_);
      }
      if (!std::isfinite(current_) && valid_(event.current, false)) {
        current_ = event.current;
        current_number_->publish_state(current_);
      }
      break;
    case EventType::STATUS:
      status_sensor_->publish_state(event.message);
      break;
    case EventType::INPUT:
      if (event.source == "web") status_sensor_->publish_state(event.message);
      break;
    case EventType::TELEMETRY: {
      const auto &state = bus_->snapshot();
      output_published_valid_ = state.telemetry_fresh(millis());
      if (output_voltage_sensor_)
        output_voltage_sensor_->publish_state(output_published_valid_ ? state.output_voltage : NAN);
      if (output_current_sensor_)
        output_current_sensor_->publish_state(output_published_valid_ ? state.output_current : NAN);
      if (telemetry_sensor_) telemetry_sensor_->publish_state(output_published_valid_);
      break;
    }
    case EventType::RAW_STATUS:
      if (raw_sensor_) raw_sensor_->publish_state(event.message);
      break;
    default:
      break;
  }
}
}
