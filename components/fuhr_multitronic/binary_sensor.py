import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TAMPER,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_FUHR_ID, FuhrMultitronic

DEPENDENCIES = ["fuhr_multitronic"]

CONF_CONNECTED = "connected"
CONF_FAULT = "fault"
CONF_TAMPER = "tamper"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FUHR_ID): cv.use_id(FuhrMultitronic),
        cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # On for 0x0D, off on any other message from the lock.
        cv.Optional(CONF_FAULT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        # On for 0x18, off at 0x16 (see README: the lock sends nothing else
        # while the tamper state lasts).
        cv.Optional(CONF_TAMPER): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_TAMPER,
        ),
    }
)

SETTERS = {
    CONF_CONNECTED: "set_connected_sensor",
    CONF_FAULT: "set_fault_sensor",
    CONF_TAMPER: "set_tamper_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FUHR_ID])
    for key, setter in SETTERS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(parent, setter)(sens))
