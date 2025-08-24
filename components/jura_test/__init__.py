import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    ICON_COUNTER,
    UNIT_EMPTY,
)

# Define the namespace for our C++ code
jura_test_ns = cg.esphome_ns.namespace("jura_test")
JuraCoffeeComponent = jura_test_ns.class_("JuraCoffeeComponent", cg.PollingComponent, uart.UARTDevice)

# Define keys for our YAML configuration
CONF_SINGLE_ESPRESSO = "single_espresso"
CONF_TANK_STATUS = "tank_status"

# Configuration schema - all sensors optional, no validation required
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JuraCoffeeComponent),
            cv.Optional(CONF_SINGLE_ESPRESSO): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_COUNTER,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_TANK_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:cup-water"
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

# Code generation
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set up sensors if configured
    if CONF_SINGLE_ESPRESSO in config:
        sens = await sensor.new_sensor(config[CONF_SINGLE_ESPRESSO])
        cg.add(var.set_single_espresso_sensor(sens))

    if CONF_TANK_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TANK_STATUS])
        cg.add(var.set_tank_status_sensor(sens))
