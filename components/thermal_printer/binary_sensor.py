import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID
from . import thermal_printer_ns, ThermalPrinterComponent, CONF_THERMAL_PRINTER_ID

DEPENDENCIES = ["thermal_printer"]

ThermalPrinterBinarySensor = thermal_printer_ns.class_(
    "ThermalPrinterBinarySensor", binary_sensor.BinarySensor, cg.Component
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    ThermalPrinterBinarySensor
).extend({
    cv.GenerateID(CONF_THERMAL_PRINTER_ID): cv.use_id(ThermalPrinterComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)
    await cg.register_component(var, config)
    
    parent = await cg.get_variable(config[CONF_THERMAL_PRINTER_ID])
    cg.add(var.set_parent(parent))
