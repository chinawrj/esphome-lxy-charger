#include "charger_indicator.h"
#include "esphome/core/log.h"

namespace esphome::charger_indicator {
void ChargerIndicator::setup() {
  if (!bus_ || !output_ || !bus_->subscribe([this](const Event &event) { controller_.observe(event, millis()); })) {
    ESP_LOGE("charger_indicator", "Cannot initialize event-bus LED");
    if (output_) output_->turn_off();
    mark_failed();
    return;
  }
  output_->turn_off();
}

void ChargerIndicator::loop() {
  if (is_failed()) return;
  const bool next = controller_.level(bus_->snapshot(), millis());
  if (next != level_) {
    level_ = next;
    output_->set_state(next);
  }
}
}
