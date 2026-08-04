import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_FUHR_ID, FuhrMultitronic

DEPENDENCIES = ["fuhr_multitronic"]

CONF_MESSAGE = "message"
CONF_DOOR = "door"
CONF_LOCK = "lock"
CONF_DAY_LATCH = "day_latch"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FUHR_ID): cv.use_id(FuhrMultitronic),
        cv.Optional(CONF_MESSAGE): text_sensor.text_sensor_schema(
            icon="mdi:message-text-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_DOOR): text_sensor.text_sensor_schema(icon="mdi:door"),
        cv.Optional(CONF_LOCK): text_sensor.text_sensor_schema(icon="mdi:lock"),
        cv.Optional(CONF_DAY_LATCH): text_sensor.text_sensor_schema(
            icon="mdi:weather-sunny"
        ),
    }
)

SETTERS = {
    CONF_MESSAGE: "set_message_sensor",
    CONF_DOOR: "set_door_sensor",
    CONF_LOCK: "set_lock_sensor",
    CONF_DAY_LATCH: "set_day_latch_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_FUHR_ID])
    for key, setter in SETTERS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(parent, setter)(sens))
