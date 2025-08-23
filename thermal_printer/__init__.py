import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@user"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor", "binary_sensor"]

thermal_printer_ns = cg.esphome_ns.namespace("thermal_printer")
ThermalPrinterComponent = thermal_printer_ns.class_("ThermalPrinterComponent", cg.Component, uart.UARTDevice)

CONF_THERMAL_PRINTER_ID = "thermal_printer_id"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThermalPrinterComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # Add required libraries
    cg.add_library("Adafruit Thermal Printer Library", "1.4.3")
    
    # Add include for Arduino compatibility
    cg.add_global(cg.global_ns.using("Print"))
