import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@arnemauer"]
AUTO_LOAD = ["text_sensor", "binary_sensor"]
MULTI_CONF = True

fuhr_ns = cg.esphome_ns.namespace("fuhr_multitronic")
FuhrMultitronic = fuhr_ns.class_("FuhrMultitronic", cg.Component)
MessageTrigger = fuhr_ns.class_(
    "MessageTrigger", automation.Trigger.template(cg.uint8, cg.std_string, cg.bool_)
)

CONF_BUS_PIN = "bus_pin"
CONF_CTRL_PIN = "ctrl_pin"
CONF_BAUD_RATE = "baud_rate"
CONF_TIMEOUT = "timeout"
CONF_PUBLISH_ACKS = "publish_acks"
CONF_ON_MESSAGE = "on_message"

CONF_FUHR_ID = "fuhr_multitronic_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FuhrMultitronic),
        cv.Required(CONF_BUS_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_CTRL_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_BAUD_RATE, default=2000): cv.int_range(min=100, max=50000),
        cv.Optional(
            CONF_TIMEOUT, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PUBLISH_ACKS, default=False): cv.boolean,
        cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MessageTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    bus = await cg.gpio_pin_expression(config[CONF_BUS_PIN])
    cg.add(var.set_bus_pin(bus))
    if CONF_CTRL_PIN in config:
        ctrl = await cg.gpio_pin_expression(config[CONF_CTRL_PIN])
        cg.add(var.set_ctrl_pin(ctrl))
    cg.add(var.set_baud_rate(config[CONF_BAUD_RATE]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_publish_acks(config[CONF_PUBLISH_ACKS]))

    for conf in config.get(CONF_ON_MESSAGE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.uint8, "code"), (cg.std_string, "name"), (cg.bool_, "from_controller")],
            conf,
        )
