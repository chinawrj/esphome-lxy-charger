#include "lxy_charger.h"
#include "esphome/core/log.h"
#include <esp_gattc_api.h>

namespace esphome::lxy_charger {
static const char *const TAG = "lxy_charger";
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 5000;
static constexpr uint32_t CONNECTION_REQUEST_TIMEOUT_MS = 30000;
static constexpr uint32_t STATUS_INTERVAL_MS = 2000;
static constexpr uint16_t MIN_VOLTAGE = 582;
static constexpr uint16_t MAX_VOLTAGE = 584;
static constexpr uint16_t MIN_CURRENT = 49;
static constexpr uint16_t MAX_CURRENT = 51;

void LXYCharger::setup() {
  this->node_state = esp32_ble_tracker::ClientState::IDLE;
  if (!this->bus_ || !this->bus_->subscribe([this](const Event &event) { this->on_event_(event); })) {
    ESP_LOGE(TAG, "Cannot subscribe to mandatory event bus; BLE disabled");
    this->parent()->set_enabled(false);
    this->mark_failed();
    return;
  }
  this->publish_connection_();
  this->publish_status_("Disconnected; settings are never applied automatically");
}

void LXYCharger::dump_config() {
  ESP_LOGCONFIG(TAG, "LXY BLE service %s: FFF0 / notify FFF1 / write FFF2", this->parent()->address_str());
  ESP_LOGCONFIG(TAG, "Event requests only; captured limits 58.2..58.4 V / 4.9..5.1 A");
}

bool LXYCharger::ready_() const {
  return this->transport_ready_ && this->config_fresh_ && this->parent_ &&
         this->parent_->state() == esp32_ble_tracker::ClientState::ESTABLISHED;
}

bool LXYCharger::publish_(Event event) {
  event.source = "ble";
  // Once faulted, only loop() may publish the recovery notices. Teardown
  // callbacks must not replace the original operation's correlation ID.
  if (this->overflow_pending_) return false;
  if (this->bus_->publish(event)) return true;
  if (event.type == EventType::RAW_STATUS) {
    ESP_LOGW(TAG, "Dropped raw status: event queue full");
    return false;
  }
  // A lost lifecycle/result event could leave UI readiness stale. Fail closed,
  // stop further writes and retry only the invalidation notice from loop().
  // This never retries the charger command itself.
  ESP_LOGE(TAG, "Critical event queue overflow; disabling BLE without replay");
  this->overflow_request_id_ = event.request_id ? event.request_id : this->active_request_id_;
  this->overflow_pending_ = true;
  this->overflow_connection_sent_ = false;
  this->overflow_status_sent_ = false;
  this->reset_link_state_();
  this->preserve_disconnect_status_ = true;
  this->parent()->set_enabled(false);
  return false;
}

void LXYCharger::publish_status_(const char *message, Result result, uint32_t request_id) {
  ESP_LOGI(TAG, "request=%lu %s", (unsigned long) request_id, message);
  Event event{};
  event.type = EventType::STATUS;
  event.request_id = request_id;
  event.result = result;
  event.message = message;
  event.connected = this->link_connected_;
  event.ready = this->ready_();
  event.busy = this->busy_();
  this->publish_(event);
}

void LXYCharger::publish_connection_(uint32_t request_id) {
  Event event{};
  event.type = EventType::CONNECTION;
  event.request_id = request_id;
  event.connected = this->link_connected_;
  event.ready = this->ready_();
  event.busy = this->busy_();
  this->publish_(event);
}

void LXYCharger::on_event_(const Event &event) {
  if (event.type != EventType::REQUEST_CONNECT && event.type != EventType::REQUEST_DISCONNECT &&
      event.type != EventType::REQUEST_READ_CONFIG && event.type != EventType::REQUEST_APPLY_CONFIG) return;
  if (this->is_failed()) return;
  if (this->overflow_pending_) {
    // The bus queue itself is bounded at 32. Preserve queued request IDs and
    // reject them after the fault notices, without invoking the BLE transport.
    if (this->overflow_rejected_count_ < this->overflow_rejected_requests_.size()) {
      this->overflow_rejected_requests_[this->overflow_rejected_count_++] = event.request_id;
    } else {
      ESP_LOGE(TAG, "Overflow request backlog full; request %lu cannot be serviced", (unsigned long) event.request_id);
    }
    return;
  }
  if (this->busy_()) {
    this->publish_status_("Request rejected: another transaction is pending", Result::REJECTED, event.request_id);
    return;
  }
  switch (event.type) {
    case EventType::REQUEST_CONNECT: {
      const bool already_ready = this->ready_();
      this->connection_request_id_ = already_ready ? 0 : event.request_id;
      this->connection_requested_at_ = millis();
      this->parent()->set_enabled(true);
      this->publish_connection_(event.request_id);
      this->publish_status_(already_ready ? "Already connected" : "BLE connection requested",
                            already_ready ? Result::VERIFIED : Result::ACCEPTED, event.request_id);
      break;
    }
    case EventType::REQUEST_DISCONNECT:
      this->parent()->set_enabled(false);
      this->reset_link_state_();
      this->preserve_disconnect_status_ = true;
      this->publish_connection_(event.request_id);
      this->publish_status_("BLE connection disabled", Result::VERIFIED, event.request_id);
      break;
    case EventType::REQUEST_READ_CONFIG:
      this->request_config_(event.request_id);
      break;
    case EventType::REQUEST_APPLY_CONFIG:
      this->apply_settings_(event.voltage, event.current, event.request_id);
      break;
    default:
      break;
  }
}

std::string LXYCharger::hex_(const uint8_t *data, size_t size) {
  static const char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    result.push_back(digits[data[i] >> 4]);
    result.push_back(digits[data[i] & 15]);
  }
  return result;
}

void LXYCharger::reset_link_state_() {
  this->link_connected_ = false;
  this->transport_ready_ = false;
  this->config_fresh_ = false;
  this->subscribe_pending_ = false;
  this->initial_query_due_ = false;
  this->query_pending_ = false;
  this->set_pending_ = false;
  this->echo_received_ = false;
  this->readback_sent_ = false;
  this->active_request_id_ = this->connection_request_id_ = 0;
  this->tx_handle_ = this->rx_handle_ = this->cccd_handle_ = 0;
  this->decoder_.reset();
}

void LXYCharger::invalidate_link_(const char *reason, Result result) {
  const uint32_t request_id = this->active_request_id_ ? this->active_request_id_ : this->connection_request_id_;
  this->reset_link_state_();
  this->preserve_disconnect_status_ = true;
  this->publish_connection_(request_id);
  this->publish_status_(reason, result, request_id);
  this->status_set_warning();
  this->parent()->disconnect();
}

bool LXYCharger::to_tenths_(float value, uint16_t &result) {
  if (!std::isfinite(value) || value < 0.0f || value > 6553.5f) return false;
  const float scaled = value * 10.0f;
  const float rounded = std::round(scaled);
  if (std::fabs(scaled - rounded) > 0.001f) return false;
  result = static_cast<uint16_t>(rounded);
  return true;
}

bool LXYCharger::send_frame_(uint8_t *frame, size_t size) {
  if (!this->transport_ready_ || !this->tx_handle_ ||
      this->parent()->state() != esp32_ble_tracker::ClientState::ESTABLISHED) {
    this->invalidate_link_("BLE unavailable; request not confirmed", this->set_pending_ ? Result::UNKNOWN : Result::FAILED);
    return false;
  }
  ESP_LOGD(TAG, "TX %s", hex_(frame, size).c_str());
  const auto result = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->tx_handle_,
      size, frame, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "GATT write failed: %d", result);
    this->invalidate_link_("Write failed; outcome unknown; reconnecting without replay");
    return false;
  }
  return true;
}

bool LXYCharger::send_(uint8_t command, const uint8_t *payload, size_t size) {
  uint8_t frame[protocol::MaxFrameSize];
  const size_t length = protocol::encode(command, payload, size, frame);
  return length && this->send_frame_(frame, length);
}

bool LXYCharger::request_config_(uint32_t request_id) {
  if (!this->transport_ready_) {
    this->publish_status_("Not ready: connect the charger first", Result::REJECTED, request_id);
    return false;
  }
  if (this->query_pending_ || (this->set_pending_ && !this->echo_received_)) {
    this->publish_status_("Request rejected: another transaction is pending", Result::REJECTED, request_id);
    return false;
  }
  this->active_request_id_ = request_id;
  this->initial_query_due_ = false;
  this->query_pending_ = true;
  this->query_started_ = millis();
  if (this->set_pending_) this->readback_sent_ = true;
  const uint8_t key = 0x01;
  if (!this->send_(0x02, &key, 1)) return false;
  this->publish_status_(this->set_pending_ ? "Echo matched; checking fresh configuration" : "Reading configuration",
                        Result::ACCEPTED, request_id);
  return true;
}

bool LXYCharger::apply_settings_(float voltage_value, float current_value, uint32_t request_id) {
  if (!this->ready_()) {
    this->publish_status_("Apply rejected: connect and read configuration first", Result::REJECTED, request_id);
    return false;
  }
  if (this->busy_()) {
    this->publish_status_("Apply rejected: another transaction is pending", Result::REJECTED, request_id);
    return false;
  }
  uint16_t voltage, current;
  if (!to_tenths_(voltage_value, voltage) || !to_tenths_(current_value, current) ||
      voltage < MIN_VOLTAGE || voltage > MAX_VOLTAGE || current < MIN_CURRENT || current > MAX_CURRENT) {
    this->publish_status_("Apply rejected: use 0.1 steps within captured limits", Result::REJECTED, request_id);
    return false;
  }
  // Snapshot the EVENT payload. No UI state is referenced, restored or stored.
  this->active_request_id_ = request_id;
  this->expected_voltage_ = voltage;
  this->expected_current_ = current;
  this->set_pending_ = true;
  this->echo_received_ = false;
  this->readback_sent_ = false;
  this->set_started_ = millis();
  uint8_t frame[10];
  const size_t length = protocol::encodeSet(voltage, current, frame);
  if (!this->send_frame_(frame, length)) return false;
  this->publish_status_("Settings sent once; waiting for matching echo", Result::ACCEPTED, request_id);
  return true;
}

void LXYCharger::handle_frame_(const uint8_t *frame, size_t size) {
  if (!this->transport_ready_) return;
  ESP_LOGD(TAG, "RX %s", hex_(frame, size).c_str());
  const uint8_t command = frame[3];
  if (command == 0x84) {
    Event event{};
    event.type = EventType::RAW_STATUS;
    event.message = hex_(frame, size);
    this->publish_(event);
    return;
  }
  if (size != 10 || frame[4] != 0x01 || (command != 0x82 && command != 0x83)) return;
  const uint16_t voltage = protocol::readU16(frame + 5);
  const uint16_t current = protocol::readU16(frame + 7);
  if (command == 0x83) {
    if (!this->set_pending_) return;
    if (voltage != this->expected_voltage_ || current != this->expected_current_) {
      this->invalidate_link_("Set echo mismatch; outcome unknown; reconnecting without replay");
      return;
    }
    this->echo_received_ = true;
    return;
  }
  const uint32_t request_id = this->query_pending_ ? this->active_request_id_ : 0;
  ESP_LOGI(TAG, "Readback setpoints: %.1f V / %.1f A", voltage / 10.0f, current / 10.0f);
  Event event{};
  event.type = EventType::CONFIG;
  event.request_id = request_id;
  event.voltage = voltage / 10.0f;
  event.current = current / 10.0f;
  event.result = Result::INFO;
  if (!this->publish_(event)) return;
  if (!this->query_pending_) return;  // Unsolicited values cannot complete a transaction.
  this->query_pending_ = false;
  this->config_fresh_ = true;
  this->connection_request_id_ = 0;
  this->status_clear_warning();
  if (this->set_pending_ && this->readback_sent_) {
    this->set_pending_ = false;
    const bool matched = voltage == this->expected_voltage_ && current == this->expected_current_;
    this->publish_connection_(request_id);
    this->publish_status_(matched ? "Verified: current configuration matches Apply" : "ERROR: configuration differs from Apply",
                          matched ? Result::VERIFIED : Result::FAILED, request_id);
  } else {
    this->publish_connection_(request_id);
    this->publish_status_("Configuration read; draft edits require explicit Apply", Result::VERIFIED, request_id);
  }
  this->active_request_id_ = this->connection_request_id_ = 0;
}

void LXYCharger::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t,
                                    esp_ble_gattc_cb_param_t *param) {
  if (this->is_failed()) return;
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      const uint32_t request_id = this->connection_request_id_;
      this->disconnect_reported_ = false;
      this->preserve_disconnect_status_ = false;
      this->reset_link_state_();
      this->link_connected_ = param->open.status == ESP_GATT_OK;
      this->connection_request_id_ = this->link_connected_ ? request_id : 0;
      this->publish_connection_(request_id);
      this->publish_status_(this->link_connected_ ? "Connected; discovering GATT" : "BLE connection failed",
                            this->link_connected_ ? Result::INFO : Result::FAILED, request_id);
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT: {
      const bool uncertain = this->set_pending_;
      const bool interrupted = this->busy_();
      const uint32_t request_id = this->active_request_id_ ? this->active_request_id_ : this->connection_request_id_;
      this->reset_link_state_();
      if (!this->disconnect_reported_) this->publish_connection_(request_id);
      if (!this->disconnect_reported_ && !this->preserve_disconnect_status_)
        this->publish_status_(uncertain ? "Disconnected during Apply; outcome unknown; no replay" : "Disconnected",
                              uncertain ? Result::UNKNOWN : (interrupted ? Result::FAILED : Result::INFO), request_id);
      this->disconnect_reported_ = true;
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (param->search_cmpl.status != ESP_GATT_OK) {
        this->invalidate_link_("GATT discovery failed", Result::FAILED);
        break;
      }
      auto *tx = this->parent()->get_characteristic(0xFFF0, 0xFFF2);
      auto *rx = this->parent()->get_characteristic(0xFFF0, 0xFFF1);
      if (!tx || !rx || !(tx->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ||
          !(rx->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
        this->invalidate_link_("Required FFF0 / FFF2 / FFF1 properties missing", Result::FAILED);
        break;
      }
      auto *cccd = rx->get_descriptor(0x2902);
      if (!cccd) {
        this->invalidate_link_("Notification CCCD missing", Result::FAILED);
        break;
      }
      this->tx_handle_ = tx->handle;
      this->rx_handle_ = rx->handle;
      this->cccd_handle_ = cccd->handle;
      this->subscribe_pending_ = true;
      this->subscribe_started_ = millis();
      if (this->parent()->register_for_notify(this->rx_handle_) != ESP_OK)
        this->invalidate_link_("Notification registration failed", Result::FAILED);
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle == this->rx_handle_) {
        if (param->reg_for_notify.status != ESP_GATT_OK)
          this->invalidate_link_("Notification registration rejected", Result::FAILED);
        else
          this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      }
      break;
    case ESP_GATTC_WRITE_DESCR_EVT:
      if (this->subscribe_pending_ && param->write.handle == this->cccd_handle_) {
        if (param->write.status != ESP_GATT_OK) {
          this->invalidate_link_("Notification subscription rejected", Result::FAILED);
          break;
        }
        this->subscribe_pending_ = false;
        this->transport_ready_ = true;
        this->initial_query_due_ = true;
        this->last_status_poll_ = millis();
        this->publish_connection_(this->connection_request_id_);
        this->publish_status_("Notifications ready; reading configuration", Result::INFO, this->connection_request_id_);
      }
      break;
    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->tx_handle_ && param->write.status != ESP_GATT_OK)
        this->invalidate_link_("GATT write completion failed; outcome unknown; no replay");
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (this->transport_ready_ && param->notify.handle == this->rx_handle_ &&
          param->notify.conn_id == this->parent()->get_conn_id()) {
        this->decoder_.feed(param->notify.value, param->notify.value_len,
            [this](const uint8_t *frame, size_t size) { this->handle_frame_(frame, size); });
      }
      break;
    default:
      break;
  }
}

void LXYCharger::loop() {
  if (this->is_failed()) return;
  if (this->overflow_pending_) {
    Event event{};
    event.source = "ble";
    event.request_id = this->overflow_request_id_;
    if (!this->overflow_connection_sent_) {
      event.type = EventType::CONNECTION;  // defaults: disconnected/not-ready/not-busy
      if (!this->bus_->publish(event)) return;
      this->overflow_connection_sent_ = true;
    }
    if (!this->overflow_status_sent_) {
      event.type = EventType::STATUS;
      event.result = Result::UNKNOWN;
      event.message = "Event queue overflow; BLE disabled; no settings replayed";
      if (!this->bus_->publish(event)) return;
      this->overflow_status_sent_ = true;
    }
    while (this->overflow_rejected_count_) {
      event.type = EventType::STATUS;
      event.request_id = this->overflow_rejected_requests_[0];
      event.result = Result::REJECTED;
      event.message = "Request rejected during event queue recovery; reconnect explicitly";
      if (!this->bus_->publish(event)) return;
      --this->overflow_rejected_count_;
      for (size_t i = 0; i < this->overflow_rejected_count_; ++i)
        this->overflow_rejected_requests_[i] = this->overflow_rejected_requests_[i + 1];
    }
    this->overflow_pending_ = false;
    this->overflow_connection_sent_ = false;
    this->overflow_status_sent_ = false;
    return;
  }
  if (this->transport_ready_ && this->parent()->state() != esp32_ble_tracker::ClientState::ESTABLISHED) {
    const bool uncertain = this->set_pending_;
    const bool interrupted = this->busy_();
    const uint32_t request_id = this->active_request_id_ ? this->active_request_id_ : this->connection_request_id_;
    this->reset_link_state_();
    this->publish_connection_(request_id);
    if (!this->preserve_disconnect_status_)
      this->publish_status_(uncertain ? "Link lost during Apply; outcome unknown; no replay" : "BLE link unavailable",
                            uncertain ? Result::UNKNOWN : (interrupted ? Result::FAILED : Result::INFO), request_id);
    this->disconnect_reported_ = true;
  }
  if (this->decoder_.checksumErrors != this->reported_checksum_errors_) {
    this->reported_checksum_errors_ = this->decoder_.checksumErrors;
    ESP_LOGW(TAG, "Discarded invalid frame checksum; total %lu", (unsigned long) this->reported_checksum_errors_);
  }
  if (this->connection_request_id_ && millis() - this->connection_requested_at_ >= CONNECTION_REQUEST_TIMEOUT_MS) {
    this->invalidate_link_("BLE connection request timed out", Result::FAILED);
    return;
  }
  if (this->subscribe_pending_ && millis() - this->subscribe_started_ >= RESPONSE_TIMEOUT_MS) {
    this->invalidate_link_("Notification subscription timed out", Result::FAILED);
    return;
  }
  if (!this->transport_ready_) return;
  if (this->set_pending_ && millis() - this->set_started_ >= RESPONSE_TIMEOUT_MS) {
    this->invalidate_link_("Apply timed out; outcome unknown; reconnecting without replay");
    return;
  }
  if (this->query_pending_ && millis() - this->query_started_ >= RESPONSE_TIMEOUT_MS) {
    this->invalidate_link_("Configuration query timed out; reconnecting to discard late replies",
                          this->set_pending_ ? Result::UNKNOWN : Result::FAILED);
    return;
  }
  if (this->initial_query_due_) {
    this->initial_query_due_ = false;
    this->request_config_(this->connection_request_id_);
  } else if (this->set_pending_ && this->echo_received_ && !this->readback_sent_ && !this->query_pending_) {
    this->request_config_(this->active_request_id_);
  }
  if (this->transport_ready_ && !this->busy_() && millis() - this->last_status_poll_ >= STATUS_INTERVAL_MS) {
    this->last_status_poll_ = millis();
    this->send_(0x04);
  }
}
}
