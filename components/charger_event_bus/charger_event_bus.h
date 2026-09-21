#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "event_core.h"

namespace esphome::charger_event_bus {

class ChargerEventBus : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool publish(const Event &event) { return this->core_.publish(event); }
  bool subscribe(EventCore::Callback callback) { return this->core_.subscribe(std::move(callback)); }
  uint32_t request(EventType type, const std::string &source, float voltage = NAN, float current = NAN) {
    return this->core_.request(type, source, voltage, current);
  }
  const Snapshot &snapshot() const { return this->core_.snapshot(); }

 protected:
  EventCore core_;
  uint32_t reported_dropped_events_{0};
};

class EventTrigger : public Trigger<const Event &> {
 public:
  explicit EventTrigger(ChargerEventBus *parent) {
    this->registered_ = parent->subscribe([this](const Event &event) { this->trigger(event); });
  }
  bool registered() const { return this->registered_; }

 protected:
  bool registered_{false};
};

}  // namespace esphome::charger_event_bus
