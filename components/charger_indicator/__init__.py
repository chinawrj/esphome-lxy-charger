"""Optional event-bus LED, independent of buttons, display and web."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.components.charger_event_bus import ChargerEventBus
from esphome.const import CONF_ID

DEPENDENCIES = ["charger_event_bus", "output"]
ns = cg.esphome_ns.namespace("charger_indicator")
ChargerIndicator = ns.class_("ChargerIndicator", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ChargerIndicator),
    cv.Required("event_bus_id"): cv.use_id(ChargerEventBus),
    cv.Required("output_id"): cv.use_id(output.BinaryOutput),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_event_bus(await cg.get_variable(config["event_bus_id"])))
    cg.add(var.set_output(await cg.get_variable(config["output_id"])))
