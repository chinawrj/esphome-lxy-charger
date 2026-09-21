"""Capture-verified BLE service; every UI/configuration boundary uses events."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID

DEPENDENCIES = ["ble_client", "charger_event_bus"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("lxy_charger")
LXYCharger = ns.class_("LXYCharger", cg.Component, ble_client.BLEClientNode)
ChargerEventBus = cg.esphome_ns.namespace("charger_event_bus").class_("ChargerEventBus", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LXYCharger),
    cv.Required("event_bus_id"): cv.use_id(ChargerEventBus),
}).extend(cv.COMPONENT_SCHEMA).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    bus = await cg.get_variable(config["event_bus_id"])
    cg.add(var.set_event_bus(bus))
