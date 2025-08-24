import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    ICON_COUNTER,
    UNIT_EMPTY,
)

# Define the namespace for our C++ code
jura_ns = cg.esphome_ns.namespace("jura")
JuraCoffeeComponent = jura_ns.class_("JuraCoffeeComponent", cg.PollingComponent, uart.UARTDevice)

# Define keys for our YAML configuration
CONF_SINGLE_ESPRESSO = "single_espresso"
CONF_DOUBLE_ESPRESSO = "double_espresso"
CONF_COFFEE = "coffee"
CONF_DOUBLE_COFFEE = "double_coffee"
CONF_CLEANINGS = "cleanings"
CONF_TRAY_STATUS = "tray_status"
CONF_TANK_STATUS = "tank_status"
CONF_TIMEOUT_MS = "timeout_ms"

def validate_config(config):
    """Ensure at least one sensor is configured"""
    sensors = [CONF_SINGLE_ESPRESSO, CONF_DOUBLE_ESPRESSO, CONF_COFFEE, 
               CONF_DOUBLE_COFFEE, CONF_CLEANINGS, CONF_TRAY_STATUS, CONF_TANK_STATUS]
    
    if not any(sensor in config for sensor in sensors):
        raise cv.Invalid("At least one sensor must be configured")
    return config

# Define the configuration schema for the component.
# This tells ESPHome what options are available in the YAML.
CONFIG_SCHEMA = cv.All(
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
            cv.Optional(CONF_TRAY_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:tray"
            ),
            cv.Optional(CONF_TANK_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:cup-water"
            ),
            cv.Optional(CONF_TIMEOUT_MS, default=5000): cv.positive_int,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA),
    validate_config
)

# This function generates the C++ code during compilation
async def to_code(config):
    # Create a new instance of our C++ class
    var = cg.new_Pvariable(config[CONF_ID])
    
    # Register the component and UART device with ESPHome
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set the timeout
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT_MS]))

    # Dictionary to map YAML keys to sensor types and C++ setter methods
    SENSORS = {
        CONF_SINGLE_ESPRESSO: (sensor.new_sensor, "set_single_espresso_sensor"),
        CONF_DOUBLE_ESPRESSO: (sensor.new_sensor, "set_double_espresso_sensor"),
        CONF_COFFEE: (sensor.new_sensor, "set_coffee_sensor"),
        CONF_DOUBLE_COFFEE: (sensor.new_sensor, "set_double_coffee_sensor"),
        CONF_CLEANINGS: (sensor.new_sensor, "set_cleanings_sensor"),
        CONF_TRAY_STATUS: (text_sensor.new_text_sensor, "set_tray_status_sensor"),
        CONF_TANK_STATUS: (text_sensor.new_text_sensor, "set_tank_status_sensor"),
    }

    # Loop through the sensors defined in the YAML and set them up
    for conf_key, (new_sensor_func, setter_method) in SENSORS.items():
        if conf_key in config:
            sens = await new_sensor_func(config[conf_key])
            cg.add(getattr(var, setter_method)(sens))
