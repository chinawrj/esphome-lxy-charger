#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/charger_event_bus/charger_event_bus.h"
#include "charger_protocol.h"
#include <array>
#include <cmath>
#include <string>

namespace esphome::lxy_charger {

class LXYCharger : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                          esp_ble_gattc_cb_param_t *param) override;
  void set_event_bus(charger_event_bus::ChargerEventBus *bus) { bus_ = bus; }

 protected:
  using Event = charger_event_bus::Event;
  using EventType = charger_event_bus::EventType;
  using Result = charger_event_bus::Result;
  void on_event_(const Event &event);
  bool ready_() const;
  bool busy_() const { return query_pending_ || set_pending_ || connection_request_id_ != 0; }
  bool request_config_(uint32_t request_id);
  bool apply_settings_(float voltage, float current, uint32_t request_id);
  bool send_(uint8_t command, const uint8_t *payload = nullptr, size_t size = 0);
  bool send_frame_(uint8_t *frame, size_t size);
  void handle_frame_(const uint8_t *frame, size_t size);
  void publish_status_(const char *message, Result result = Result::INFO, uint32_t request_id = 0);
  void publish_connection_(uint32_t request_id = 0);
  bool publish_(Event event);
  void reset_link_state_();
  void invalidate_link_(const char *reason, Result result = Result::UNKNOWN);
  static std::string hex_(const uint8_t *data, size_t size);
  static bool to_tenths_(float value, uint16_t &result);

  charger_event_bus::ChargerEventBus *bus_{nullptr};
  protocol::Decoder decoder_;
  uint16_t tx_handle_{0};
  uint16_t rx_handle_{0};
  uint16_t cccd_handle_{0};
  uint16_t expected_voltage_{0};
  uint16_t expected_current_{0};
  uint32_t active_request_id_{0};
  uint32_t connection_request_id_{0};
  uint32_t connection_requested_at_{0};
  uint32_t query_started_{0};
  uint32_t set_started_{0};
  uint32_t subscribe_started_{0};
  uint32_t last_status_poll_{0};
  uint32_t reported_checksum_errors_{0};
  uint32_t overflow_request_id_{0};
  std::array<uint32_t, 32> overflow_rejected_requests_{};
  size_t overflow_rejected_count_{0};
  bool link_connected_{false};
  bool transport_ready_{false};
  bool config_fresh_{false};
  bool subscribe_pending_{false};
  bool initial_query_due_{false};
  bool query_pending_{false};
  bool set_pending_{false};
  bool echo_received_{false};
  bool readback_sent_{false};
  bool disconnect_reported_{false};
  bool preserve_disconnect_status_{false};
  bool overflow_pending_{false};
  bool overflow_connection_sent_{false};
  bool overflow_status_sent_{false};
};
}
