# Optional M5StickC Plus board battery

This AXP192 producer monitors the original M5StickC Plus / 1.1 internal battery.
It publishes `BOARD_BATTERY` only; it has no LCD, Button, LED, Web or BLE object
references. Include `packages/m5stickc-plus-battery.yaml` independently. The LCD
and Battery packages share one mapped I2C bus definition, so either may be omitted.
ATOMS3U and Plus2 must not include this board-specific package.

Every 2 s, the driver checks AXP192 identity, enables only ADC bits 7/6 in register
0x82 using read/modify/write, and reads battery presence and six ADC bytes. It
waits until a later poll after enabling the ADC. Charge control, cutoff, power
rails, USB limits, other ADC bits and ADC rate are untouched.

VBAT is the 12-bit value at 0x78/79 × 1.1 mV. Charge and discharge are separate
13-bit values at 0x7A/7B and 0x7C/7D × 0.5 mA. Reserved low-register bits are
masked. The reducer computes **charge − discharge**: positive charges the board
battery, negative discharges it. USB presence alone does not determine direction.

Sources: [AXP192 datasheet, pages 26/32/33/41](https://dl.linux-sunxi.org/AXP/AXP192%20Datasheet%20v1.13.pdf),
[M5Stack AXP192 implementation](https://github.com/m5stack/M5Unified/blob/master/src/utility/power/AXP192_Class.cpp),
[M5Stack signed-current convention](https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.hpp).

A failed read, wrong chip, disabled ADC or absent battery never yields a fake
zero. Read errors invalidate the sample and are retried at the normal interval;
missing updates expire after 6 s. Battery presence is distinguished from an I2C
error. Battery data survives BLE disconnection and cannot create charger output
telemetry or change charging setpoints. Native tests compile the real producer
against fake registers and assert the only write is preserved-bit ADC enable.

The home footer shows `本机电池`, volts and `充 +...mA` / `放 -...mA`; exactly zero
shows `0.0mA`. Its footer temporarily yields to held-button prompts. Full controls
remain on the Help page. The dedicated W/V/A meter stays unchanged. INFO serial
logs report the board sample every 10 s for hardware checks.
