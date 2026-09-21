"""Optional LCD view model, shared by firmware and documentation previews."""
import esphome.config_validation as cv

DEPENDENCIES = ["charger_event_bus", "display", "font"]
CONFIG_SCHEMA = cv.Schema({})

async def to_code(config):
    pass
