import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

# Define the namespace for our C++ code
jura_test_ns = cg.esphome_ns.namespace("jura_test")
JuraCoffeeComponent = jura_test_ns.class_("JuraCoffeeComponent", cg.PollingComponent, uart.UARTDevice)

# Configuration schema - all sensors optional, no validation required
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JuraCoffeeComponent),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

# Code generation - no sensors for now
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
