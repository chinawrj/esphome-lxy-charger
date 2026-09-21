#pragma once

#include "../charger_event_bus/event_core.h"
#include <array>
#include <cstdio>
#include <string>

namespace esphome::charger_display {
// Portable view model. The device and preview tool consume the same text and
// coordinates; there is no BLE, Web, Button or GPIO dependency here.
enum class Font { SMALL, MEDIUM, LARGE };
enum class Ink { WHITE, MUTED, GREEN, AMBER };
struct Label {
  int x{0}, y{0};
  Font font{Font::SMALL};
  Ink ink{Ink::WHITE};
  bool right{false};
  std::string text;
};
struct View {
  std::array<Label, 12> labels{};
  size_t count{0};
  void add(int x, int y, Font font, Ink ink, const std::string &text, bool right = false) {
    if (count < labels.size()) labels[count++] = {x, y, font, ink, right, text};
  }
};
inline std::string number(float value) {
  if (!std::isfinite(value)) return "--.-";
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}
inline View make_view(const charger_event_bus::Snapshot &state, uint32_t now) {
  using namespace charger_event_bus;
  View view;
  const bool editing = state.ui_mode != UiMode::VIEW && state.ui_mode != UiMode::REFRESHING;
  if (editing) {
    const bool adjust = state.ui_mode == UiMode::EDIT;
    const bool confirm = state.ui_mode == UiMode::CONFIRM;
    view.add(8, 3, Font::MEDIUM, Ink::AMBER,
        adjust ? (state.ui_field == UiField::VOLTAGE ? "EDIT VOLTAGE" : "EDIT CURRENT") :
        (confirm ? "APPLY THESE SETTINGS?" : "VERIFYING SETTINGS..."));
    view.add(8, 29, Font::LARGE, state.ui_field == UiField::VOLTAGE ? Ink::AMBER : Ink::WHITE,
        number(state.ui_voltage));
    view.add(132, 29, Font::LARGE, state.ui_field == UiField::CURRENT ? Ink::AMBER : Ink::WHITE,
        number(state.ui_current));
    view.add(8, 75, Font::SMALL, Ink::MUTED, "SET VOLTAGE / V");
    view.add(132, 75, Font::SMALL, Ink::MUTED, "SET CURRENT / A");
    if (adjust) {
      view.add(8, 99, Font::MEDIUM, Ink::WHITE, "A -0.1     B +0.1");
      view.add(8, 120, Font::SMALL, Ink::MUTED, "Hold A: next     Hold B: cancel");
    } else if (confirm) {
      view.add(8, 99, Font::MEDIUM, Ink::AMBER, "Hold A again to apply");
      view.add(8, 120, Font::SMALL, Ink::MUTED, "B: cancel     No automatic save");
    } else {
      view.add(8, 104, Font::MEDIUM, Ink::MUTED, "Waiting for fresh readback");
    }
    return view;
  }
  const bool fresh = state.telemetry_fresh(now);
  const bool problem = state.result == Result::UNKNOWN || state.result == Result::FAILED ||
      state.result == Result::REJECTED;
  view.add(8, 3, Font::SMALL, Ink::MUTED, "OUTPUT");
  view.add(232, 3, Font::SMALL, fresh ? Ink::GREEN : Ink::AMBER,
      !state.connected ? "OFFLINE" : (fresh ? "LIVE" : "NO LIVE DATA"), true);
  view.add(8, 23, Font::LARGE, fresh ? Ink::WHITE : Ink::MUTED,
      fresh ? number(state.output_voltage) : "--.-");
  view.add(132, 23, Font::LARGE, fresh ? Ink::WHITE : Ink::MUTED,
      fresh ? number(state.output_current) : "--.-");
  view.add(8, 69, Font::SMALL, Ink::MUTED, "VOLTAGE / V");
  view.add(132, 69, Font::SMALL, Ink::MUTED, "CURRENT / A");
  const std::string v = state.ready ? number(state.voltage) : "--.-";
  const std::string a = state.ready ? number(state.current) : "--.-";
  view.add(8, 92, Font::MEDIUM, state.ui_buttons_ready && state.ui_field == UiField::VOLTAGE ? Ink::AMBER : Ink::MUTED,
      "Set " + v + " V");
  view.add(132, 92, Font::MEDIUM, state.ui_buttons_ready && state.ui_field == UiField::CURRENT ? Ink::AMBER : Ink::MUTED,
      "Set " + a + " A");
  view.add(8, 120, Font::SMALL, problem ? Ink::AMBER : Ink::MUTED,
      (state.busy || state.ui_mode == UiMode::REFRESHING) ? "Reading settings..." : (problem ? "Last request not verified" :
      (state.ui_buttons_ready ? "A: select / hold edit    B: refresh" :
       "Live output / readback setpoints")));
  return view;
}
}  // namespace esphome::charger_display
