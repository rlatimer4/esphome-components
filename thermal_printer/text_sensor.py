import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from . import thermal_printer_ns, ThermalPrinterComponent, CONF_THERMAL_PRINTER_ID

DEPENDENCIES = ["thermal_printer"]

ThermalPrinterTextSensor = thermal_printer_ns.class_("ThermalPrinterTextSensor", text_sensor.TextSensor, cg.Component)

CONFIG_SCHEMA = text_sensor.TEXT_SENSOR_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(ThermalPrinterTextSensor),
        cv.GenerateID(CONF_THERMAL_PRINTER_ID): cv.use_id(ThermalPrinterComponent),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)
    await cg.register_component(var, config)
    
    parent = await cg.get_variable(config[CONF_THERMAL_PRINTER_ID])
    cg.add(var.set_parent(parent))
