#pragma once
#include "esphome/core/component.h"
#include "esphome/components/charger_event_bus/charger_event_bus.h"
#include "buttons_controller.h"

namespace esphome::charger_buttons {
class ChargerButtons : public Component {
 public:
  void set_event_bus(charger_event_bus::ChargerEventBus *bus) { bus_ = bus; }
  void setup() override;
  void loop() override;
 protected:
  void on_event_(const charger_event_bus::Event &event);
  charger_event_bus::ChargerEventBus *bus_{nullptr};
  ButtonsController controller_;
  bool controls_pending_{true};
  bool controls_available_{false};
};
}
