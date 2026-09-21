"""Queued intermodule events and event-derived state for the LXY controller."""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ON_EVENT, CONF_TRIGGER_ID

MULTI_CONF = True
ns = cg.esphome_ns.namespace("charger_event_bus")
ChargerEventBus = ns.class_("ChargerEventBus", cg.Component)
Event = ns.struct("Event")
EventType = ns.enum("EventType", is_class=True)
Result = ns.enum("Result", is_class=True)
EventTrigger = ns.class_("EventTrigger", automation.Trigger.template(Event.operator("const").operator("ref")))

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ChargerEventBus),
        cv.Optional(CONF_ON_EVENT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(EventTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for conf in config.get(CONF_ON_EVENT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(Event.operator("const").operator("ref"), "event")], conf
        )
