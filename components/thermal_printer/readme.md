# ESPHome Thermal Printer Integration

A **pure custom ESPHome implementation** for thermal printers, specifically designed for ESP8266 D1 Mini boards with 58mm thermal printers. This integration provides direct ESC/POS command control without external library dependencies.

## Key Features

✅ **No External Dependencies** - Pure ESPHome implementation  
✅ **Paper Usage Tracking** - Persistent counters with flash storage  
✅ **Enhanced Formatting** - Two-column text, table printing, dot leaders  
✅ **Home Assistant Integration** - Full service and sensor support  
✅ **Memory Efficient** - Optimized for ESP8266 constraints  
✅ **Reliable Communication** - Direct UART control with proper error handling

## Hardware Requirements

- **ESP8266 D1 Mini** (or compatible)
- **58mm Thermal Printer** (CSN-A2-T compatible)
- **5V 2A Power Supply** for the printer
- **Jumper wires** for connections
- **Logic level converter** (optional but recommended for reliable communication)

## Wiring

| ESP8266 D1 Mini | Thermal Printer | Notes |
|------------------|-----------------|--------|
| 5V (VIN)         | VCC/VH          | Power for printer |
| GND              | GND             | Common ground |
| GPIO4 (D2)       | TX/RXD          | Data to printer |
| GPIO0 (D3)       | RX/TXD          | Optional (not used for printing) |

⚠️ **Important**: Some thermal printers operate at 5V logic levels while ESP8266 uses 3.3V. Consider using a logic level converter for reliable communication.

## Software Installation

### 1. Directory Structure

Create the following directory structure in your ESPHome configuration folder:

```
config/
├── thermal_printer.yaml
├── secrets.yaml
└── my_components/
    └── thermal_printer/
        ├── __init__.py
        ├── thermal_printer.h
        ├── thermal_printer.cpp
        ├── text_sensor.py
        ├── text_sensor.h
        ├── text_sensor.cpp
        ├── binary_sensor.py
        ├── binary_sensor.h
        └── binary_sensor.cpp
```

### 2. Configuration Files

1. **Copy the component files** to `my_components/thermal_printer/`
2. **Copy the device configuration** to `thermal_printer.yaml`
3. **Create secrets.yaml** with your WiFi credentials:

```yaml
wifi_ssid: "YourWiFiNetwork"
wifi_password: "YourWiFiPassword"
api_encryption_key: "your_32_char_base64_key"
ota_password: "YourOTAPassword"
```

### 3. Compile and Upload

```bash
# Validate configuration
esphome config thermal_printer.yaml

# Compile and upload
esphome run thermal_printer.yaml
```

## Features

### Core Functionality
- **Text printing** with multiple sizes (Small, Medium, Large)
- **Text formatting**: Bold, Underline, Inverse
- **Text alignment**: Left, Center, Right
- **Two-column printing** with customizable dot leaders
- **Table printing** (2 or 3 columns with auto-alignment)
- **Barcode printing** (UPC-A, UPC-E, EAN13, EAN8, CODE39, ITF, CODABAR, CODE93, CODE128)
- **Paper status detection** with real-time monitoring
- **Power management** (Sleep/Wake)
- **Line spacing control**
- **Feed paper control**

### Paper Usage Tracking
- **Real-time tracking** of lines printed, characters printed, paper feeds
- **Usage estimation** in millimeters and percentage of roll
- **Persistent storage** in ESP flash memory (survives reboots)
- **Configurable roll length** (default 30m)
- **Usage reset functionality**
- **Historical counters** for maintenance planning

### ESPHome Integration
- **Home Assistant API** integration
- **Services** for all printer functions including new formatting features
- **Sensors** for paper status and usage monitoring
- **Switches** for power management
- **Automatic startup** message
- **Status monitoring**
- **Template sensors** for usage analytics

## Usage Examples

### Basic Text Printing

```yaml
# In Home Assistant automation
- service: esphome.thermal_printer_print_text
  data:
    text: "Hello World!"
    size: "M"          # S, M, or L
    justify: "C"       # L, C, or R  
    bold: true
    underline: false
    inverse: false
```

### Barcode Printing

```yaml
- service: esphome.thermal_printer_print_barcode
  data:
    barcode_type: 8    # CODE128
    barcode_data: "123456789012"
```

### Two-Column Printing

```yaml
# Print receipt-style layouts
- service: esphome.thermal_printer_print_two_column
  data:
    left_text: "Coffee"
    right_text: "$3.50"
    fill_dots: true    # Creates: Coffee...........$3.50

- service: esphome.thermal_printer_print_two_column
  data:
    left_text: "Tax"
    right_text: "$0.25"
    fill_dots: true    # Creates: Tax................$0.25
```

### Table Printing

```yaml
# Three-column table
- service: esphome.thermal_printer_print_table_row
  data:
    col1: "Item"
    col2: "Qty"
    col3: "Price"

- service: esphome.thermal_printer_print_table_row
  data:
    col1: "Coffee"
    col2: "2"
    col3: "$7.00"
```

### Paper Usage Management

```yaml
# Check paper usage
sensor_value: "{{ states('sensor.thermal_printer_paper_usage_percent') }}%"

# Reset paper usage counters
- service: esphome.thermal_printer_reset_paper_usage

# Set custom roll length (in mm)
- service: esphome.thermal_printer_set_paper_roll_length
  data:
    length_mm: 50000  # 50 meter roll
```

## Available Services

| Service | Parameters | Description |
|---------|------------|-------------|
| `print_text` | `text`, `size`, `justify`, `bold`, `underline`, `inverse` | Print formatted text |
| `print_two_column` | `left_text`, `right_text`, `fill_dots` | Print two-column layout with optional dot leaders |
| `print_table_row` | `col1`, `col2`, `col3` | Print table row (2 or 3 columns) |
| `print_barcode` | `barcode_type`, `barcode_data` | Print barcode |
| `feed_paper` | `lines` | Feed paper by specified lines |
| `wake_printer` | - | Wake printer from sleep |
| `sleep_printer` | - | Put printer to sleep |
| `test_print` | - | Print test page |
| `reset_paper_usage` | - | Reset paper usage counters |
| `set_paper_roll_length` | `length_mm` | Set paper roll length for usage calculation |

## Barcode Types

| Code | Type | Characters | Notes |
|------|------|------------|--------|
| 0 | UPC-A | 11-12 | Universal Product Code |
| 1 | UPC-E | 11-12 | Universal Product Code (compressed) |
| 2 | EAN13 | 12-13 | European Article Number |
| 3 | EAN8 | 7-8 | European Article Number (short) |
| 4 | CODE39 | 1-255 | Code 39 |
| 5 | ITF | Even number | Interleaved 2 of 5 |
| 6 | CODABAR | 1-255 | Codabar |
| 7 | CODE93 | 1-255 | Code 93 |
| 8 | CODE128 | 2-255 | Code 128 (recommended) |

## Monitoring and Status

### Sensors Available
- **Paper Status** (Text): "Present" or "Out"
- **Paper Loaded** (Binary): True/False
- **Paper Usage (mm)** (Sensor): Millimeters of paper used
- **Paper Usage (%)** (Sensor): Percentage of roll used (based on 30m default)
- **Lines Printed** (Sensor): Total lines printed since last reset
- **Characters Printed** (Sensor): Total characters printed since last reset
- **WiFi Signal**: Signal strength
- **Uptime**: Device uptime

### Controls Available
- **Printer Wake Switch**: Wake/Sleep printer
- **Line Spacing Number**: Adjust line spacing (24-64)
- **Default Text Size Select**: Set default text size

## Troubleshooting

### Common Issues

1. **Printer not responding**
   - Check power supply (needs 5V 2A minimum)
   - Verify wiring connections
   - Try using logic level converter
   - Check if printer is in sleep mode

2. **Garbled text**
   - Wrong baud rate (should be 9600)
   - Voltage level issues (use logic level converter)
   - Poor connections

3. **Paper jam detection false positives**
   - Clean paper sensors
   - Check paper width (should be 57.5-58mm)
   - Adjust paper detection sensitivity in code

4. **Compilation errors**
   - Ensure all component files are in correct locations
   - Check ESPHome version compatibility
   - Verify YAML syntax

### Debug Mode

Enable debug logging in your YAML:

```yaml
logger:
  level: DEBUG
  logs:
    thermal_printer: DEBUG
```

### Testing Sequence

1. **Power on test**: Check if printer initializes
2. **Communication test**: Send wake command
3. **Print test**: Use built-in test page
4. **Paper test**: Check paper status sensor
5. **Format test**: Try different text sizes and formatting

## Advanced Configuration

### Custom Heat Settings

Adjust heat settings for different paper types:

```yaml
# In lambda functions
id(my_thermal_printer)->set_heat_config(dots, time, interval);
// dots: heating dots (7-11)
// time: heat time (80-120) 
// interval: heat interval (2-50)
```

### Print Speed Optimization

For faster printing, reduce heat settings:
- Lower heat dots value
- Reduce heat time
- Increase heat interval

For better print quality, increase heat settings:
- Higher heat dots value
- Increase heat time  
- Lower heat interval

## Home Assistant Integration

### Dashboard Card

```yaml
type: entities
title: Thermal Printer
entities:
  - entity: binary_sensor.thermal_printer_paper_loaded
    name: Paper Status
  - entity: switch.thermal_printer_printer_wake  
    name: Printer Power
  - entity: number.thermal_printer_line_spacing
    name: Line Spacing
```

### Automation Examples

See `home_assistant_examples.yaml` for complete automation examples including:
- Daily weather reports
- Shopping list printing
- Doorbell notifications
- System status reports

## Contributing

This integration is based on the Adafruit Thermal Printer Library and modernized for ESPHome. Feel free to contribute improvements via pull requests.

## License

This project inherits the license from the Adafruit Thermal Printer Library (BSD license).

## Credits

- **Adafruit** for the original thermal printer library
- **bcjmlegacy** for the ESP8266 MQTT reference implementation  
- **ESPHome community** for the excellent framework
