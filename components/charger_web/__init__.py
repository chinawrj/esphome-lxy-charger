"""Optional entity adapter. Every charger operation crosses the event bus."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, number, sensor, switch, text_sensor
from esphome.components.charger_event_bus import ChargerEventBus
from esphome.const import CONF_ID

DEPENDENCIES = ["charger_event_bus"]
AUTO_LOAD = ["binary_sensor", "button", "number", "sensor", "switch", "text_sensor"]
ns = cg.esphome_ns.namespace("charger_web")
ChargerWeb = ns.class_("ChargerWeb", cg.Component)
DraftNumber = ns.class_("DraftNumber", number.Number)
RequestButton = ns.class_("RequestButton", button.Button)
ConnectionSwitch = ns.class_("ConnectionSwitch", switch.Switch)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ChargerWeb),
    cv.Required("event_bus_id"): cv.use_id(ChargerEventBus),
    cv.Optional("output_voltage"): sensor.sensor_schema(unit_of_measurement="V", accuracy_decimals=1, device_class="voltage", state_class="measurement"),
    cv.Optional("output_current"): sensor.sensor_schema(unit_of_measurement="A", accuracy_decimals=1, device_class="current", state_class="measurement"),
    cv.Optional("telemetry_valid"): binary_sensor.binary_sensor_schema(),
    cv.Required("configured_voltage"): sensor.sensor_schema(unit_of_measurement="V", accuracy_decimals=1),
    cv.Required("configured_current"): sensor.sensor_schema(unit_of_measurement="A", accuracy_decimals=1),
    cv.Required("requested_voltage"): number.number_schema(DraftNumber, unit_of_measurement="V"),
    cv.Required("requested_current"): number.number_schema(DraftNumber, unit_of_measurement="A"),
    cv.Required("apply"): button.button_schema(RequestButton),
    cv.Required("refresh"): button.button_schema(RequestButton),
    cv.Required("connection"): binary_sensor.binary_sensor_schema(),
    cv.Required("connection_switch"): switch.switch_schema(ConnectionSwitch),
    cv.Required("transaction_status"): text_sensor.text_sensor_schema(),
    cv.Optional("link_status"): text_sensor.text_sensor_schema(),
    cv.Optional("output_data_status"): text_sensor.text_sensor_schema(),
    cv.Optional("raw_status"): text_sensor.text_sensor_schema(),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config["event_bus_id"])
    cg.add(var.set_event_bus(bus))
    for key in ("configured_voltage", "configured_current"):
        child = await sensor.new_sensor(config[key])
        cg.add(getattr(var, "set_" + key)(child))
    for key in ("output_voltage", "output_current"):
        if key in config:
            child = await sensor.new_sensor(config[key])
            cg.add(getattr(var, "set_" + key)(child))
    if "telemetry_valid" in config:
        child = await binary_sensor.new_binary_sensor(config["telemetry_valid"])
        cg.add(var.set_telemetry_valid(child))
    for key, voltage, minimum, maximum in (
        ("requested_voltage", True, 58.2, 58.4),
        ("requested_current", False, 4.9, 5.1),
    ):
        child = await number.new_number(config[key], var, voltage, min_value=minimum, max_value=maximum, step=0.1)
        cg.add(getattr(var, "set_" + key)(child))
    for key, apply in (("apply", True), ("refresh", False)):
        await button.new_button(config[key], var, apply)
    child = await binary_sensor.new_binary_sensor(config["connection"])
    cg.add(var.set_connection(child))
    child = await switch.new_switch(config["connection_switch"], var)
    cg.add(var.set_connection_switch(child))
    for key in ("transaction_status", "raw_status", "link_status", "output_data_status"):
        if key in config:
            child = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, "set_" + key)(child))
