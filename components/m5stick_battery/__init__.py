"""Optional original M5StickC Plus AXP192 battery event producer."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.components.charger_event_bus import ChargerEventBus
from esphome.const import CONF_ID
DEPENDENCIES = ["i2c", "charger_event_bus"]
ns = cg.esphome_ns.namespace("m5stick_battery")
M5StickBattery = ns.class_("M5StickBattery", cg.PollingComponent, i2c.I2CDevice)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(M5StickBattery),
    cv.Required("event_bus_id"): cv.use_id(ChargerEventBus),
}).extend(cv.polling_component_schema("2s")).extend(i2c.i2c_device_schema(0x34))
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_event_bus(await cg.get_variable(config["event_bus_id"])))
