#include "charger_event_bus.h"
#include "esphome/core/log.h"

namespace esphome::charger_event_bus {
static const char *const TAG = "charger_event_bus";

void ChargerEventBus::setup() {
  ESP_LOGI(TAG, "Event bus ready; queued dispatch only");
}

void ChargerEventBus::loop() {
  this->core_.dispatch();
  const uint32_t dropped = this->core_.dropped_events();
  if (dropped != this->reported_dropped_events_) {
    this->reported_dropped_events_ = dropped;
    ESP_LOGW(TAG, "Event queue overflow; %lu event(s) rejected", static_cast<unsigned long>(dropped));
  }
}

void ChargerEventBus::dump_config() {
  ESP_LOGCONFIG(TAG, "Charger Event Bus: %u subscribers, queue capacity %u, dispatch budget %u",
                static_cast<unsigned>(this->core_.subscriber_count()),
                static_cast<unsigned>(EventCore::QUEUE_CAPACITY),
                static_cast<unsigned>(EventCore::DEFAULT_DISPATCH_BUDGET));
}

}  // namespace esphome::charger_event_bus
