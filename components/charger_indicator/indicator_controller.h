#pragma once
#include "esphome/components/charger_event_bus/event_core.h"

namespace esphome::charger_indicator {
using namespace charger_event_bus;

class IndicatorController {
 public:
  bool suspended(const Snapshot &state) const {
    return state.ui_buttons_ready && !state.ui_backlight_on;
  }
  bool level(const Snapshot &state, uint32_t now) const {
    if (suspended(state)) return false;  // Zero duty: no idle blink/pulse.
    // Link state is deliberately independent of protocol readiness, operations,
    // charger output and measurement decoding. This LED has one meaning only.
    if (state.connected) return true;
    return state.connection_enabled && now % 1000 < 500;
  }
};
}
