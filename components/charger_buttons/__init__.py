"""Optional two-button editor. All application traffic uses the event bus."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.charger_event_bus import ChargerEventBus
from esphome.const import CONF_ID

DEPENDENCIES = ["charger_event_bus"]
ns = cg.esphome_ns.namespace("charger_buttons")
ChargerButtons = ns.class_("ChargerButtons", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ChargerButtons),
    cv.Required("event_bus_id"): cv.use_id(ChargerEventBus),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_event_bus(await cg.get_variable(config["event_bus_id"])))
