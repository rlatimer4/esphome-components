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
CONF_BUFFER_SIZE = "buffer_size"

# Enhanced configuration schema with Phase 1 features
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
            
            # Performance tuning
            cv.Optional(CONF_BUFFER_SIZE, default=512): cv.int_range(min=128, max=2048),
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
    
    # Warn about very conservative settings
    if heat_intensity < 200:
        import logging
        _LOGGER = logging.getLogger(__name__)
        _LOGGER.warning(
            f"Heat settings very conservative (intensity: {heat_intensity}). "
            f"Print quality may be poor."
        )
    
    return config

def validate_paper_settings(config):
    """Validate paper-related settings"""
    paper_width = config.get(CONF_PAPER_WIDTH, 58)
    chars_per_line = config.get(CONF_CHARS_PER_LINE, 32)
    
    # Calculate realistic characters per line based on paper width
    # Typical thermal printer: ~0.125mm per character for small text
    expected_chars = int(paper_width / 1.8)  # Conservative estimate
    
    if chars_per_line > expected_chars * 1.2:
        raise cv.Invalid(
            f"chars_per_line ({chars_per_line}) too high for paper_width ({paper_width}mm). "
            f"Maximum recommended: {expected_chars}"
        )
    
    return config

def validate_performance_settings(config):
    """Validate performance-related settings"""
    buffer_size = config.get(CONF_BUFFER_SIZE, 512)
    
    # Ensure buffer size is reasonable for ESP8266
    if buffer_size > 1024:
        import logging
        _LOGGER = logging.getLogger(__name__)
        _LOGGER.warning(
            f"Large buffer size ({buffer_size}) may cause memory issues on ESP8266"
        )
    
    return config

# Apply all validation functions
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    validate_heat_settings,
    validate_paper_settings,
    validate_performance_settings
)

async def to_code(config):
    """Generate the component code with enhanced configuration"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # Configure basic printer settings
    cg.add(var.set_paper_width(config[CONF_PAPER_WIDTH]))
    cg.add(var.set_chars_per_line(config[CONF_CHARS_PER_LINE]))
    
    # Configure heat settings
    cg.add(var.set_heat_dots(config[CONF_HEAT_DOTS]))
    cg.add(var.set_heat_time(config[CONF_HEAT_TIME]))
    cg.add(var.set_heat_interval(config[CONF_HEAT_INTERVAL]))
    
    # Configure power management
    cg.add(var.set_auto_sleep(config[CONF_AUTO_SLEEP]))
    
    # Configure paper management
    cg.add(var.set_paper_roll_length(config[CONF_PAPER_ROLL_LENGTH]))
    cg.add(var.set_line_height_calibration(config[CONF_LINE_HEIGHT_CALIBRATION]))
    
    # Configure Phase 1 features
    cg.add(var.set_rotation_enabled(config[CONF_ENABLE_ROTATION]))
    cg.add(var.set_qr_codes_enabled(config[CONF_ENABLE_QR_CODES]))
    cg.add(var.set_startup_message_enabled(config[CONF_STARTUP_MESSAGE]))
    
    # Configure performance settings
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    
    # Add C++ definitions for new enums and structs
    cg.add_define("THERMAL_PRINTER_BUFFER_SIZE", config[CONF_BUFFER_SIZE])
    
    # Add conditional compilation flags
    if config[CONF_ENABLE_ROTATION]:
        cg.add_define("THERMAL_PRINTER_ENABLE_ROTATION")
    
    if config[CONF_ENABLE_QR_CODES]:
        cg.add_define("THERMAL_PRINTER_ENABLE_QR_CODES")
    
    # Generate configuration summary
    cg.add_library("thermal_printer", "Enhanced Thermal Printer Component")
    
    # Add build flags for optimization
    cg.add_build_flag("-DTHERMAL_PRINTER_ENHANCED")
    cg.add_build_flag("-Os")  # Optimize for size on ESP8266

# Helper function for service definitions
def generate_service_schema():
    """Generate schema for Home Assistant services"""
    return {
        # Enhanced print_text service
        "print_text": {
            "description": "Print formatted text with rotation support",
            "fields": {
                "message": {"description": "Text to print", "required": True},
                "text_size": {"description": "Text size (S/M/L)", "default": "M"},
                "alignment": {"description": "Text alignment (L/C/R)", "default": "L"},
                "bold": {"description": "Bold text", "default": False},
                "underline": {"description": "Underlined text", "default": False},
                "inverse": {"description": "Inverse text", "default": False},
                "rotation": {"description": "Rotation (0-3 for 0°/90°/180°/270°)", "default": 0}
            }
        },
        
        # New rotated text service
        "print_rotated_text": {
            "description": "Print text with 90-degree rotation",
            "fields": {
                "message": {"description": "Text to print", "required": True},
                "rotation": {"description": "Rotation (1=90°, 2=180°, 3=270°)", "default": 1}
            }
        },
        
        # New QR code service
        "print_qr_code": {
            "description": "Print QR code with optional label",
            "fields": {
                "data": {"description": "QR code data", "required": True},
                "size": {"description": "QR code size (1-4)", "default": 3},
                "error_correction": {"description": "Error correction (0-3)", "default": 1},
                "label": {"description": "Optional label text", "default": ""}
            }
        },
        
        # Enhanced two-column service
        "print_two_column": {
            "description": "Print two-column layout with size support",
            "fields": {
                "left_text": {"description": "Left column text", "required": True},
                "right_text": {"description": "Right column text", "required": True},
                "fill_dots": {"description": "Fill with dots", "default": True},
                "text_size": {"description": "Text size (S/M/L)", "default": "S"}
            }
        },
        
        # New template services
        "print_receipt": {
            "description": "Print formatted receipt",
            "fields": {
                "business_name": {"description": "Business name", "required": True},
                "items": {"description": "Items (item1:price1,item2:price2)", "required": True},
                "total": {"description": "Total amount", "required": True},
                "date": {"description": "Date (auto if empty)", "default": ""},
                "time": {"description": "Time (auto if empty)", "default": ""}
            }
        },
        
        "print_shopping_list": {
            "description": "Print formatted shopping list",
            "fields": {
                "items": {"description": "Comma-separated items", "required": True}
            }
        },
        
        # New utility services
        "safe_print_text": {
            "description": "Print text with error checking",
            "fields": {
                "message": {"description": "Text to print", "required": True}
            }
        },
        
        "check_paper_sufficiency": {
            "description": "Check if enough paper for job",
            "fields": {
                "estimated_lines": {"description": "Lines needed", "required": True}
            }
        },
        
        "set_heat_config": {
            "description": "Configure heat settings",
            "fields": {
                "dots": {"description": "Heat dots (1-15)", "default": 7},
                "time": {"description": "Heat time (50-200)", "default": 80},
                "interval": {"description": "Heat interval (1-10)", "default": 2},
                "density": {"description": "Print density (1-15)", "default": 4}
            }
        }
    }

# Configuration validation functions
def validate_uart_config(config):
    """Validate UART configuration for thermal printer"""
    # Check if UART device is properly configured
    if uart.CONF_UART_ID not in config:
        raise cv.Invalid("uart_id is required for thermal printer")
    
    return config

def validate_feature_compatibility(config):
    """Check feature compatibility with hardware"""
    if config.get(CONF_ENABLE_ROTATION, True):
        # Most thermal printers support rotation, but warn about older models
        import logging
        _LOGGER = logging.getLogger(__name__)
        _LOGGER.info("Rotation enabled - ensure your thermal printer supports ESC V commands")
    
    if config.get(CONF_ENABLE_QR_CODES, True):
        # QR codes require more advanced thermal printers
        import logging
        _LOGGER = logging.getLogger(__name__)
        _LOGGER.info("QR codes enabled - ensure your printer supports GS ( k commands")
    
    return config

def generate_usage_estimation_code(config):
    """Generate code for paper usage estimation based on configuration"""
    roll_length = config.get(CONF_PAPER_ROLL_LENGTH, 30000)
    line_height = config.get(CONF_LINE_HEIGHT_CALIBRATION, 4.0)
    chars_per_line = config.get(CONF_CHARS_PER_LINE, 32)
    
    return f"""
    // Generated usage estimation parameters
    static const float PAPER_ROLL_LENGTH = {roll_length}f;
    static const float LINE_HEIGHT_MM = {line_height}f;
    static const uint8_t CHARS_PER_LINE = {chars_per_line};
    """

# Apply additional validation
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    validate_uart_config,
    validate_feature_compatibility
)

async def to_code(config):
    """Enhanced code generation with Phase 1 features"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # Generate configuration methods
    if CONF_PAPER_WIDTH in config:
        cg.add(var.set_paper_width(config[CONF_PAPER_WIDTH]))
    
    if CONF_CHARS_PER_LINE in config:
        cg.add(var.set_chars_per_line(config[CONF_CHARS_PER_LINE]))
    
    if CONF_HEAT_DOTS in config:
        cg.add(var.set_heat_dots(config[CONF_HEAT_DOTS]))
    
    if CONF_HEAT_TIME in config:
        cg.add(var.set_heat_time(config[CONF_HEAT_TIME]))
    
    if CONF_HEAT_INTERVAL in config:
        cg.add(var.set_heat_interval(config[CONF_HEAT_INTERVAL]))
    
    if CONF_AUTO_SLEEP in config:
        cg.add(var.set_auto_sleep_enabled(config[CONF_AUTO_SLEEP]))
    
    if CONF_PAPER_ROLL_LENGTH in config:
        cg.add(var.set_paper_roll_length(config[CONF_PAPER_ROLL_LENGTH]))
    
    if CONF_LINE_HEIGHT_CALIBRATION in config:
        cg.add(var.set_line_height_calibration(config[CONF_LINE_HEIGHT_CALIBRATION]))
    
    if CONF_STARTUP_MESSAGE in config:
        cg.add(var.set_startup_message_enabled(config[CONF_STARTUP_MESSAGE]))
    
    if CONF_ENABLE_ROTATION in config:
        cg.add(var.set_rotation_enabled(config[CONF_ENABLE_ROTATION]))
    
    if CONF_ENABLE_QR_CODES in config:
        cg.add(var.set_qr_codes_enabled(config[CONF_ENABLE_QR_CODES]))
    
    if CONF_BUFFER_SIZE in config:
        cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    
    # Add compile-time definitions
    cg.add_define("THERMAL_PRINTER_PAPER_WIDTH", config.get(CONF_PAPER_WIDTH, 58))
    cg.add_define("THERMAL_PRINTER_CHARS_PER_LINE", config.get(CONF_CHARS_PER_LINE, 32))
    cg.add_define("THERMAL_PRINTER_BUFFER_SIZE", config.get(CONF_BUFFER_SIZE, 512))
    
    # Feature flags
    if config.get(CONF_ENABLE_ROTATION, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_ROTATION")
    
    if config.get(CONF_ENABLE_QR_CODES, True):
        cg.add_define("THERMAL_PRINTER_ENABLE_QR_CODES")
    
    # Add configuration summary to build log
    paper_width = config.get(CONF_PAPER_WIDTH, 58)
    roll_length = config.get(CONF_PAPER_ROLL_LENGTH, 30000) / 1000
    heat_config = f"{config.get(CONF_HEAT_DOTS, 7)}/{config.get(CONF_HEAT_TIME, 80)}/{config.get(CONF_HEAT_INTERVAL, 2)}"
    
    cg.add_library("ThermalPrinter", None)
    
    # Add include guards for Phase 1 features
    cg.add_library("ThermalPrinterRotation", None, ["THERMAL_PRINTER_ENABLE_ROTATION"])
    cg.add_library("ThermalPrinterQR", None, ["THERMAL_PRINTER_ENABLE_QR_CODES"])
    
    # Generate logging statements for configuration
    cg.add(cg.RawStatement(f'''
    ESP_LOGCONFIG("{thermal_printer_ns}", "Thermal Printer Configuration:");
    ESP_LOGCONFIG("{thermal_printer_ns}", "  Paper: {paper_width}mm x {roll_
