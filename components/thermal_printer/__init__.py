import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@user"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor", "binary_sensor"]

thermal_printer_ns = cg.esphome_ns.namespace("thermal_printer")
ThermalPrinterComponent = thermal_printer_ns.class_("ThermalPrinterComponent", cg.Component, uart.UARTDevice)

# Configuration constants
CONF_THERMAL_PRINTER_ID = "thermal_printer_id"
CONF_PAPER_WIDTH = "paper_width"
CONF_CHARS_PER_LINE = "chars_per_line"
CONF_HEAT_DOTS = "heat_dots"
CONF_HEAT_TIME = "heat_time"
CONF_HEAT_INTERVAL = "heat_interval"
CONF_AUTO_SLEEP = "auto_sleep"
CONF_PAPER_ROLL_LENGTH = "paper_roll_length"
CONF_LINE_HEIGHT_CALIBRATION = "line_height_calibration"
CONF_STARTUP_MESSAGE = "startup_message"
CONF_ENABLE_ROTATION = "enable_rotation"
CONF_ENABLE_QR_CODES = "enable_qr_codes"

# Simplified configuration schema for Phase 1
CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThermalPrinterComponent),
            
            # Basic printer configuration
            cv.Optional(CONF_PAPER_WIDTH, default=58): cv.int_range(min=32, max=80),
            cv.Optional(CONF_CHARS_PER_LINE, default=32): cv.int_range(min=16, max=64),
            
            # Heat configuration for print quality
            cv.Optional(CONF_HEAT_DOTS, default=7): cv.int_range(min=1, max=15),
            cv.Optional(CONF_HEAT_TIME, default=80): cv.int_range(min=50, max=200),
            cv.Optional(CONF_HEAT_INTERVAL, default=2): cv.int_range(min=1, max=10),
            
            # Power management
            cv.Optional(CONF_AUTO_SLEEP, default=True): cv.boolean,
            
            # Paper management
            cv.Optional(CONF_PAPER_ROLL_LENGTH, default=30000): cv.positive_float,
            cv.Optional(CONF_LINE_HEIGHT_CALIBRATION, default=4.0): cv.float_range(min=1.0, max=10.0),
            
            # Feature toggles (Phase 1)
            cv.Optional(CONF_ENABLE_ROTATION, default=True): cv.boolean,
            cv.Optional(CONF_ENABLE_QR_CODES, default=True): cv.boolean,
            cv.Optional(CONF_STARTUP_MESSAGE, default=True): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

def validate_heat_settings(config):
    """Validate heat settings for safe operation"""
    dots = config.get(CONF_HEAT_DOTS, 7)
    time = config.get(CONF_HEAT_TIME, 80)
    interval = config.get(CONF_HEAT_INTERVAL, 2)
    
    # Check for potentially damaging combinations
    heat_intensity = (dots * time) / interval
    if heat_intensity > 800:
        raise cv.Invalid(
            f"Heat settings too aggressive (intensity: {heat_intensity}). "
            f"Reduce heat_dots ({dots}) or heat_time ({time}), or increase heat_interval ({interval})"
        )
    
    return config

def validate_paper_settings(config):
    """Validate paper-related settings"""
    paper_width = config.get(CONF_PAPER_WIDTH, 58)
    chars_per_line = config.get(CONF_CHARS_PER_LINE, 32)
    
    # Calculate realistic characters per line based on paper width
    expected_chars = int(paper_width / 1.8)  # Conservative estimate
    
    if chars_per_line > expected_chars * 1.2:
        raise cv.Invalid(
            f"chars_per_line ({chars_per_line}) too high for paper_width ({paper_width}mm). "
            f"Maximum recommended: {expected_chars}"
        )
    
    return config

# Apply validation functions
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    validate_heat_settings,
    validate_paper_settings
)

async def to_code(config):
    """Generate the component code with enhanced configuration"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # Only add configuration calls for methods that exist in the base component
    if CONF_PAPER_ROLL_LENGTH in config:
        cg.add(var.set_paper_roll_length(config[CONF_PAPER_ROLL_LENGTH]))
    
    if CONF_LINE_HEIGHT_CALIBRATION in config:
        cg.add(var.set_line_height_calibration(config[CONF_LINE_HEIGHT_CALIBRATION]))
    
    # Add compile-time definitions
    cg.add_define("THERMAL_PRINTER_PAPER_WIDTH", config.get(CONF_PAPER_WIDTH, 58))
    cg.add_define("THERMAL_PRINTER_CHARS_PER_LINE", config.get(CONF_CHARS_PER_LINE, 32))
    
    # Feature flags
    if config.get(CONF_ENABLE_ROTATION, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_ROTATION")
    
    if config.get(CONF_ENABLE_QR_CODES, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_QR_CODES")
