import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_ID,
    ICON_COUNTER,
    STATE_CLASS_TOTAL_INCREASING,
)

DEPENDENCIES = ["uart"]

jura_ns = cg.esphome_ns.namespace("jura")
JuraCoffeeComponent = jura_ns.class_(
    "JuraCoffeeComponent", cg.PollingComponent, uart.UARTDevice
)

CONF_SINGLE_ESPRESSO = "single_espresso"
CONF_DOUBLE_ESPRESSO = "double_espresso"
CONF_COFFEE = "coffee"
CONF_DOUBLE_COFFEE = "double_coffee"
CONF_CLEANINGS = "cleanings"
CONF_TIMEOUT_MS = "timeout_ms"

COUNTER_SCHEMA = sensor.sensor_schema(
    icon=ICON_COUNTER,
    accuracy_decimals=0,
    state_class=STATE_CLASS_TOTAL_INCREASING,
)

SENSOR_KEYS = {
    CONF_SINGLE_ESPRESSO: "set_single_espresso_sensor",
    CONF_DOUBLE_ESPRESSO: "set_double_espresso_sensor",
    CONF_COFFEE: "set_coffee_sensor",
    CONF_DOUBLE_COFFEE: "set_double_coffee_sensor",
    CONF_CLEANINGS: "set_cleanings_sensor",
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JuraCoffeeComponent),
            cv.Optional(CONF_TIMEOUT_MS, default=5000): cv.int_range(
                min=1000, max=30000
            ),
            cv.Optional(CONF_SINGLE_ESPRESSO): COUNTER_SCHEMA,
            cv.Optional(CONF_DOUBLE_ESPRESSO): COUNTER_SCHEMA,
            cv.Optional(CONF_COFFEE): COUNTER_SCHEMA,
            cv.Optional(CONF_DOUBLE_COFFEE): COUNTER_SCHEMA,
            cv.Optional(CONF_CLEANINGS): sensor.sensor_schema(
                icon="mdi:spray-bottle",
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "jura", baud_rate=9600, require_tx=True, require_rx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT_MS]))

    for key, setter in SENSOR_KEYS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))
