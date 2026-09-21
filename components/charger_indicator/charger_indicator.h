#pragma once
#include "esphome/core/component.h"
#include "esphome/components/output/binary_output.h"
#include "esphome/components/charger_event_bus/charger_event_bus.h"
#include "indicator_controller.h"

namespace esphome::charger_indicator {
class ChargerIndicator : public Component {
 public:
  void set_event_bus(charger_event_bus::ChargerEventBus *bus) { bus_ = bus; }
  void set_output(output::BinaryOutput *output) { output_ = output; }
  void setup() override;
  void loop() override;
 protected:
  charger_event_bus::ChargerEventBus *bus_{nullptr};
  output::BinaryOutput *output_{nullptr};
  IndicatorController controller_;
  bool level_{false};
};
}
