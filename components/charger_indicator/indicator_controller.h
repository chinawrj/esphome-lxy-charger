#pragma once
#include "esphome/components/charger_event_bus/event_core.h"

namespace esphome::charger_indicator {
using namespace charger_event_bus;

class IndicatorController {
 public:
  void observe(const Event &event, uint32_t now) {
    if (event.type != EventType::STATUS && event.type != EventType::UI_STATE) return;
    const auto result = event.result;
    if (result != Result::VERIFIED && result != Result::REJECTED &&
        result != Result::FAILED && result != Result::UNKNOWN) return;
    // Keep a fault visible through a quick reconnect/readback recovery.
    if (notice_active_(now) && fault_(notice_) && !fault_(result)) return;
    notice_ = result;
    notice_at_ = now;
    has_notice_ = true;
  }

  bool level(const Snapshot &state, uint32_t now) const {
    const bool active = notice_active_(now);
    const uint32_t phase = (now - notice_at_) % 1500;
    if (active && fault_(notice_))
      return phase < 100 || (phase >= 250 && phase < 350) || (phase >= 500 && phase < 600);
    if (state.busy) return now % 250 < 125;
    if (active && notice_ == Result::VERIFIED)
      return phase < 100 || (phase >= 200 && phase < 300);
    if (active && notice_ == Result::REJECTED)
      return phase < 300 || (phase >= 500 && phase < 800);
    if (state.ready) return true;
    if (state.connected) return now % 1000 < 500;
    return now % 3000 < 80;
  }

 private:
  static bool fault_(Result value) { return value == Result::FAILED || value == Result::UNKNOWN; }
  bool notice_active_(uint32_t now) const {
    return has_notice_ && now - notice_at_ < (fault_(notice_) ? 6000u : 3000u);
  }
  Result notice_{Result::INFO};
  uint32_t notice_at_{0};
  bool has_notice_{false};
};
}
