#pragma once
#include "esphome/core/component.h"
#include "esphome/components/charger_event_bus/charger_event_bus.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <cmath>

namespace esphome::charger_web {
class ChargerWeb;
class DraftNumber : public number::Number {
 public:
  DraftNumber(ChargerWeb *owner, bool voltage) : owner_(owner), voltage_(voltage) {}
 protected:
  void control(float value) override;
  ChargerWeb *owner_;
  bool voltage_;
};
class RequestButton : public button::Button {
 public:
  RequestButton(ChargerWeb *owner, bool apply) : owner_(owner), apply_(apply) {}
 protected:
  void press_action() override;
  ChargerWeb *owner_;
  bool apply_;
};
class ConnectionSwitch : public switch_::Switch {
 public:
  explicit ConnectionSwitch(ChargerWeb *owner) : owner_(owner) {}
 protected:
  void write_state(bool state) override;
  ChargerWeb *owner_;
};

class ChargerWeb : public Component {
 public:
  void setup() override;
  void loop() override;
  void set_event_bus(charger_event_bus::ChargerEventBus *bus) { bus_ = bus; }
  void set_output_voltage(sensor::Sensor *value) { output_voltage_sensor_ = value; }
  void set_output_current(sensor::Sensor *value) { output_current_sensor_ = value; }
  void set_telemetry_valid(binary_sensor::BinarySensor *value) { telemetry_sensor_ = value; }
  void set_configured_voltage(sensor::Sensor *value) { voltage_sensor_ = value; }
  void set_configured_current(sensor::Sensor *value) { current_sensor_ = value; }
  void set_requested_voltage(DraftNumber *value) { voltage_number_ = value; }
  void set_requested_current(DraftNumber *value) { current_number_ = value; }
  void set_connection(binary_sensor::BinarySensor *value) { ready_sensor_ = value; }
  void set_connection_switch(ConnectionSwitch *value) { connection_switch_ = value; }
  void set_transaction_status(text_sensor::TextSensor *value) { status_sensor_ = value; }
  void set_link_status(text_sensor::TextSensor *value) { link_status_sensor_ = value; }
  void set_output_data_status(text_sensor::TextSensor *value) { output_status_sensor_ = value; }
  void set_raw_status(text_sensor::TextSensor *value) { raw_sensor_ = value; }
  bool stage(bool voltage, float value);
  void apply();
  void refresh();
  void connect(bool enabled);
 protected:
  void on_event_(const charger_event_bus::Event &event);
  void status_(const char *message, charger_event_bus::Result result);
  static bool valid_(float value, bool voltage);
  charger_event_bus::ChargerEventBus *bus_{nullptr};
  sensor::Sensor *output_voltage_sensor_{nullptr};
  sensor::Sensor *output_current_sensor_{nullptr};
  binary_sensor::BinarySensor *telemetry_sensor_{nullptr};
  bool output_published_valid_{false};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  DraftNumber *voltage_number_{nullptr};
  DraftNumber *current_number_{nullptr};
  binary_sensor::BinarySensor *ready_sensor_{nullptr};
  ConnectionSwitch *connection_switch_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *raw_sensor_{nullptr};
  text_sensor::TextSensor *link_status_sensor_{nullptr};
  text_sensor::TextSensor *output_status_sensor_{nullptr};
  std::string published_link_status_;
  std::string published_output_status_;
  float voltage_{NAN};
  float current_{NAN};
};
}
