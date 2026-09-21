#include "../components/charger_display/charger_display.h"
#include <iostream>
using namespace esphome::charger_event_bus;
using namespace esphome::charger_display;
void emit(const char *name, const Snapshot &state, uint32_t now = 1000) {
  const View view = make_view(state, now);
  std::cout << "PAGE\t" << name << '\n';
  for (size_t i = 0; i < view.count; ++i) {
    const auto &label = view.labels[i];
    std::cout << label.x << '\t' << label.y << '\t' << static_cast<int>(label.font) << '\t'
        << static_cast<int>(label.ink) << '\t' << label.right << '\t' << label.text << '\n';
  }
}
int main() {
  Snapshot state;
  state.connected = state.connection_enabled = state.ready = state.ui_buttons_ready = true;
  state.voltage = 58.4f;
  state.current = 5.1f;
  state.ui_voltage = 58.3f;
  state.ui_current = 5.1f;
  emit("lcd-output-preview.png", state);
  state.ui_mode = UiMode::EDIT;
  emit("lcd-edit-preview.png", state);
  state.ui_mode = UiMode::CONFIRM;
  emit("lcd-confirm-preview.png", state);
  state.ui_mode = UiMode::HELP;
  emit("lcd-help-preview.png", state);
  state.ui_mode = UiMode::VIEW;
  state.telemetry_supported = state.telemetry_valid = state.telemetry_seen = true;
  state.sampled_at = 1000;
  state.output_voltage = 53.8f;
  state.output_current = 4.9f;
  emit("lcd-live-simulation.png", state);
  emit("lcd-stale-simulation.png", state, 8000);
  state.connected = state.connection_enabled = state.ready = false;
  state.telemetry_valid = false;
  emit("lcd-disconnected-preview.png", state);
}
