#include "../components/charger_display/charger_display.h"
#include <iostream>
int main() {
  using namespace esphome::charger_event_bus;
  using namespace esphome::charger_display;
  Snapshot state;
  state.connected = state.ready = state.ui_buttons_ready = true;
  state.voltage = 58.4f;
  state.current = 5.1f;
  state.ui_voltage = 58.3f;
  state.ui_current = 5.1f;
  for (const auto mode : {UiMode::VIEW, UiMode::EDIT, UiMode::CONFIRM}) {
    state.ui_mode = mode;
    const View view = make_view(state, 1000);
    std::cout << "PAGE\t" << static_cast<int>(mode) << '\n';
    for (size_t i = 0; i < view.count; ++i) {
      const auto &label = view.labels[i];
      std::cout << label.x << '\t' << label.y << '\t' << static_cast<int>(label.font) << '\t'
          << static_cast<int>(label.ink) << '\t' << label.right << '\t' << label.text << '\n';
    }
  }
}
