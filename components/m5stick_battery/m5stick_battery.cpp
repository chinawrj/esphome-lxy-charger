#include "m5stick_battery.h"
#include "esphome/core/log.h"
namespace esphome::m5stick_battery {
using namespace charger_event_bus;
static const char *const TAG = "m5stick_battery";
void M5StickBattery::setup() {
  if (!bus_) { mark_failed(); return; }
  update();
}
void M5StickBattery::update() {
  if (!bus_ || is_failed()) return;
  Event event{};
  event.type = EventType::BOARD_BATTERY;
  event.source = "m5stick_battery";
  event.sampled_at = millis();
  uint8_t chip = 0, adc = 0, status = 0, data[6]{};
  // Only enable battery V/I ADCs. Preserve every other ADC bit and leave
  // charging, rails, cutoff, USB current limits and sample rate untouched.
  if (read_byte(0x03, &chip) && chip == 0x03 && read_byte(0x82, &adc)) {
    if ((adc & 0xC0) != 0xC0) {
      if (!write_byte(0x82, adc | 0xC0)) status_set_warning();
      // Wait for a later poll; never publish pre-enable ADC contents as fresh.
    } else if (read_byte(0x01, &status)) {
      event.battery_present = (status & 0x20) != 0;
      if (!event.battery_present) event.battery_valid = true;
      else if (read_bytes(0x78, data, sizeof(data))) {
        // Datasheet: 12-bit VBAT, 13-bit charge and discharge channels.
        event.battery_voltage = ((uint16_t(data[0]) << 4) | (data[1] & 0x0F)) * 0.0011f;
        event.battery_charge_ma = ((uint16_t(data[2]) << 5) | (data[3] & 0x1F)) * 0.5f;
        event.battery_discharge_ma = ((uint16_t(data[4]) << 5) | (data[5] & 0x1F)) * 0.5f;
        event.battery_valid = true;
      }
    }
  }
  if (event.battery_valid) status_clear_warning();
  else status_set_warning();
  if (!bus_->publish(event)) { status_set_warning(); return; }
  if (event.battery_valid && event.battery_present && (!logged_ || uint32_t(millis() - last_log_at_) >= 10000)) {
    ESP_LOGI(TAG, "Battery %.3f V; charge %.1f mA; discharge %.1f mA; net %+.1f mA",
             event.battery_voltage, event.battery_charge_ma, event.battery_discharge_ma,
             event.battery_charge_ma - event.battery_discharge_ma);
    logged_ = true; last_log_at_ = millis();
  }
}
}
