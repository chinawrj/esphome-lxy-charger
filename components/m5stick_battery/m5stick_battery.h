#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/charger_event_bus/charger_event_bus.h"
namespace esphome::m5stick_battery {
class M5StickBattery : public PollingComponent, public i2c::I2CDevice {
 public:
  M5StickBattery() : PollingComponent(2000) {}
  void set_event_bus(charger_event_bus::ChargerEventBus *bus) { bus_ = bus; }
  void setup() override;
  void update() override;
 protected:
  charger_event_bus::ChargerEventBus *bus_{nullptr};
  uint32_t last_log_at_{0};
  bool logged_{false};
};
}
