import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome import pins

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

# DTR Handshaking Configuration
CONF_DTR_PIN = "dtr_pin"
CONF_ENABLE_DTR = "enable_dtr_handshaking"

# NEW: Queue System Configuration
CONF_ENABLE_QUEUE = "enable_queue_system"
CONF_MAX_QUEUE_SIZE = "max_queue_size"
CONF_PRINT_DELAY = "print_delay_ms"
CONF_AUTO_PROCESS_QUEUE = "auto_process_queue"

# Enhanced configuration schema with DTR and Queue support
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
            
            # Feature toggles
            cv.Optional(CONF_ENABLE_ROTATION, default=True): cv.boolean,
            cv.Optional(CONF_ENABLE_QR_CODES, default=True): cv.boolean,
            cv.Optional(CONF_STARTUP_MESSAGE, default=True): cv.boolean,
            
            # DTR Handshaking (Hardware Flow Control)
            cv.Optional(CONF_ENABLE_DTR, default=False): cv.boolean,
            cv.Optional(CONF_DTR_PIN): pins.internal_gpio_input_pin_schema,
            
            # NEW: Queue System Configuration
            cv.Optional(CONF_ENABLE_QUEUE, default=True): cv.boolean,
            cv.Optional(CONF_MAX_QUEUE_SIZE, default=10): cv.int_range(min=2, max=50),
            cv.Optional(CONF_PRINT_DELAY, default=2000): cv.int_range(min=500, max=10000),
            cv.Optional(CONF_AUTO_PROCESS_QUEUE, default=True): cv.boolean,
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

def validate_dtr_settings(config):
    """Validate DTR handshaking configuration"""
    enable_dtr = config.get(CONF_ENABLE_DTR, False)
    dtr_pin = config.get(CONF_DTR_PIN)
    
    if enable_dtr and dtr_pin is None:
        raise cv.Invalid(
            "DTR handshaking is enabled but no dtr_pin specified. "
            "Either disable DTR handshaking or specify a DTR pin."
        )
    
    if not enable_dtr and dtr_pin is not None:
        raise cv.Invalid(
            "DTR pin specified but DTR handshaking is disabled. "
            "Either enable DTR handshaking or remove the dtr_pin configuration."
        )
    
    return config

def validate_queue_settings(config):
    """Validate queue system configuration"""
    enable_queue = config.get(CONF_ENABLE_QUEUE, True)
    max_queue_size = config.get(CONF_MAX_QUEUE_SIZE, 10)
    print_delay = config.get(CONF_PRINT_DELAY, 2000)
    
    if enable_queue:
        if max_queue_size < 2:
            raise cv.Invalid("Queue size must be at least 2 jobs")
        
        if print_delay < 500:
            raise cv.Invalid("Print delay must be at least 500ms to prevent printer overload")
    
    return config

# Apply all validation functions
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    validate_heat_settings,
    validate_paper_settings,
    validate_dtr_settings,
    validate_queue_settings
)

async def to_code(config):
    """Generate the component code with DTR handshaking and queue system support"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # Basic configuration
    if CONF_PAPER_ROLL_LENGTH in config:
        cg.add(var.set_paper_roll_length(config[CONF_PAPER_ROLL_LENGTH]))
    
    if CONF_LINE_HEIGHT_CALIBRATION in config:
        cg.add(var.set_line_height_calibration(config[CONF_LINE_HEIGHT_CALIBRATION]))
    
    # Heat configuration
    if CONF_HEAT_DOTS in config:
        cg.add(var.set_heat_dots(config[CONF_HEAT_DOTS]))
    
    if CONF_HEAT_TIME in config:
        cg.add(var.set_heat_time(config[CONF_HEAT_TIME]))
    
    if CONF_HEAT_INTERVAL in config:
        cg.add(var.set_heat_interval(config[CONF_HEAT_INTERVAL]))
    
    # DTR Handshaking Configuration
    if config.get(CONF_ENABLE_DTR, False):
        dtr_pin = await cg.gpio_pin_expression(config[CONF_DTR_PIN])
        cg.add(var.set_dtr_pin(dtr_pin))
        cg.add(var.enable_dtr_handshaking(True))
    
    # NEW: Queue System Configuration
    if config.get(CONF_ENABLE_QUEUE, True):
        cg.add(var.set_max_queue_size(config.get(CONF_MAX_QUEUE_SIZE, 10)))
        cg.add(var.set_print_delay(config.get(CONF_PRINT_DELAY, 2000)))
        cg.add(var.enable_auto_queue_processing(config.get(CONF_AUTO_PROCESS_QUEUE, True)))
    
    # Add compile-time definitions
    cg.add_define("THERMAL_PRINTER_PAPER_WIDTH", config.get(CONF_PAPER_WIDTH, 58))
    cg.add_define("THERMAL_PRINTER_CHARS_PER_LINE", config.get(CONF_CHARS_PER_LINE, 32))
    
    # Feature flags
    if config.get(CONF_ENABLE_ROTATION, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_ROTATION")
    
    if config.get(CONF_ENABLE_QR_CODES, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_QR_CODES")
    
    if config.get(CONF_ENABLE_DTR, False):
        cg.add_define("THERMAL_PRINTER_ENABLE_DTR")
    
    if config.get(CONF_ENABLE_QUEUE, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_QUEUE")
