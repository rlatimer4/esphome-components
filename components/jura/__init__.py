import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    ICON_COUNTER,
    UNIT_EMPTY,
)

# Define the namespace for our C++ code
jura_ns = cg.esphome_ns.namespace("jura")
JuraCoffeeComponent = jura_ns.class_("JuraCoffeeComponent", cg.PollingComponent, uart.UARTDevice)

# Configuration keys - exactly like the original
CONF_SINGLE_ESPRESSO = "single_espresso"
CONF_DOUBLE_ESPRESSO = "double_espresso"
CONF_COFFEE = "coffee"
CONF_DOUBLE_COFFEE = "double_coffee"
CONF_CLEANINGS = "cleanings"
CONF_TRAY_STATUS = "tray_status"
CONF_TANK_STATUS = "tank_status"

# Simple configuration schema - no validation
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JuraCoffeeComponent),
            cv.Optional(CONF_SINGLE_ESPRESSO): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_COUNTER,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_DOUBLE_ESPRESSO): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_COUNTER,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_COFFEE): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_COUNTER,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_DOUBLE_COFFEE): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_COUNTER,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_CLEANINGS): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon="mdi:spray-bottle",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_TRAY_STATUS): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_TANK_STATUS): text_sensor.text_sensor_schema(),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

# Code generation - simplified from original template approach
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set up sensors if configured - like the original template sensors
    if CONF_SINGLE_ESPRESSO in config:
        sens = await sensor.new_sensor(config[CONF_SINGLE_ESPRESSO])
        cg.add(var.set_single_espresso_sensor(sens))

    if CONF_DOUBLE_ESPRESSO in config:
        sens = await sensor.new_sensor(config[CONF_DOUBLE_ESPRESSO])
        cg.add(var.set_double_espresso_sensor(sens))

    if CONF_COFFEE in config:
        sens = await sensor.new_sensor(config[CONF_COFFEE])
        cg.add(var.set_coffee_sensor(sens))

    if CONF_DOUBLE_COFFEE in config:
        sens = await sensor.new_sensor(config[CONF_DOUBLE_COFFEE])
        cg.add(var.set_double_coffee_sensor(sens))

    if CONF_CLEANINGS in config:
        sens = await sensor.new_sensor(config[CONF_CLEANINGS])
        cg.add(var.set_cleanings_sensor(sens))

    if CONF_TRAY_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TRAY_STATUS])
        cg.add(var.set_tray_status_sensor(sens))

    if CONF_TANK_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TANK_STATUS])
        cg.add(var.set_tank_status_sensor(sens))
