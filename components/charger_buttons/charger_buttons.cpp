#include "charger_buttons.h"
#include "esphome/core/log.h"

namespace esphome::charger_buttons {
void ChargerButtons::setup() {
  if (!bus_ || !bus_->subscribe([this](const Event &event) { on_event_(event); })) {
    ESP_LOGE("charger_buttons", "Cannot subscribe to event bus; buttons disabled");
    mark_failed();
    loop();
    return;
  }
  controller_.set_requester([this](EventType type, float voltage, float current) {
    return bus_->request(type, "buttons", voltage, current);
  });
  controls_available_ = true;
  loop();
}

void ChargerButtons::on_event_(const Event &event) {
  if (is_failed()) return;
  controller_.observe(event, bus_->snapshot(), millis());
  if (event.type != EventType::INPUT || event.source != "buttons_gpio" || !bus_->snapshot().ui_buttons_ready) return;
  if (event.message == "a_down" || event.message == "a_up" ||
      event.message == "b_down" || event.message == "b_up")
    controller_.edge(event.message[0] == 'a', event.message.size() == 6,
                     event.request_id, millis(), bus_->snapshot());
}

void ChargerButtons::loop() {
  if (is_failed() && controls_available_) {
    controls_available_ = false;
    controls_pending_ = true;
  }
  if (bus_ && controls_pending_) {
    Event event{}; event.type = EventType::UI_CONTROLS;
    event.source = "buttons";
    event.ui_buttons_ready = controls_available_ && !is_failed();
    if (bus_->publish(event)) controls_pending_ = false;
  }
  if (is_failed()) return;
  controller_.tick(millis(), bus_->snapshot());
  if (controller_.changed()) {
    if (bus_->publish(controller_.ui_event())) controller_.published();
    else controller_.display_publish_failed();
  }
}
}
