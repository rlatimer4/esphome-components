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

These examples call the Home Assistant actions that the package
(`packages/thermal-printer/thermal_printer.yaml`) defines under `api: actions:`.

### Action naming

Every action in the package is named `${printer_name}_<action>`, and Home Assistant
exposes ESPHome actions as `esphome.<device_name>_<action_name>`, where
`<device_name>` is the `esphome: name:` of the node with `-` replaced by `_`.
So the full name is:

```
esphome.<device_name>_<printer_name>_<action>
```

The examples below assume a device named `thermal-printer` and the package's
default `printer_name: "thermal_printer"`, giving for example
`esphome.thermal_printer_thermal_printer_print_text`. Adjust the prefix to match
your own device name and `printer_name` substitution (check *Developer tools →
Actions* in Home Assistant for the exact names).

Home Assistant requires every field an action declares, so all fields are shown
in each example.

### Basic Text Printing

```yaml
# In Home Assistant automation
- action: esphome.thermal_printer_thermal_printer_print_text
  data:
    message: "Hello World!"
    text_size: "M"     # S, M, or L (anything else prints as M)
    alignment: "C"     # L, C, or R (anything else prints as L)
    bold: true
    underline: false
    inverse: false
```

### Barcode Printing

```yaml
- action: esphome.thermal_printer_thermal_printer_print_barcode
  data:
    barcode_type: 8    # CODE128 (see Barcode Types below)
    barcode_data: "123456789012"
```

### QR Code Printing

```yaml
- action: esphome.thermal_printer_thermal_printer_print_qr_code
  data:
    data: "https://www.home-assistant.io"
    size: 3              # component default
    error_correction: 1  # component default
    label: "Scan me"     # printed small and centered above the code; "" for none
```

### Two-Column Printing

```yaml
# Print receipt-style layouts
- action: esphome.thermal_printer_thermal_printer_print_two_column
  data:
    left_text: "Coffee"
    right_text: "$3.50"
    fill_dots: true    # Creates: Coffee...........$3.50
    text_size: "S"     # S, M, or L (anything else prints as S)

- action: esphome.thermal_printer_thermal_printer_print_two_column
  data:
    left_text: "Tax"
    right_text: "$0.25"
    fill_dots: true    # Creates: Tax................$0.25
    text_size: "S"
```

### Table Printing

```yaml
# Three-column table
- action: esphome.thermal_printer_thermal_printer_print_table_row
  data:
    col1: "Item"
    col2: "Qty"
    col3: "Price"
    header: true

- action: esphome.thermal_printer_thermal_printer_print_table_row
  data:
    col1: "Coffee"
    col2: "2"
    col3: "$7.00"
    header: false

# Two-column table row: leave col3 empty
- action: esphome.thermal_printer_thermal_printer_print_table_row
  data:
    col1: "Subtotal"
    col2: "$7.00"
    col3: ""
    header: false
```

### Multiple Lines and Receipts

```yaml
# Print several lines (small, left-aligned), split on `separator`
- action: esphome.thermal_printer_thermal_printer_print_text_lines
  data:
    lines: "Milk|Bread|Eggs"
    separator: "|"     # "" splits on newlines

# Print a complete receipt (header, items one per line, total, footer)
- action: esphome.thermal_printer_thermal_printer_print_receipt
  data:
    business_name: "Kitchen Cafe"
    items: |
      Espresso x1
      Croissant x1
    total: "$8.50"
    add_date: true
```

### Paper Feed and Printer Control

```yaml
- action: esphome.thermal_printer_thermal_printer_feed_paper
  data:
    lines: 3

# These bypass the queue and take no data
- action: esphome.thermal_printer_thermal_printer_wake_printer
- action: esphome.thermal_printer_thermal_printer_sleep_printer
- action: esphome.thermal_printer_thermal_printer_test_print
```

### Queue Management

```yaml
- action: esphome.thermal_printer_thermal_printer_clear_queue
- action: esphome.thermal_printer_thermal_printer_process_queue
- action: esphome.thermal_printer_thermal_printer_flush_queue

- action: esphome.thermal_printer_thermal_printer_configure_queue
  data:
    max_size: 20
    delay_ms: 800
    auto_process: true
```

### Paper Usage Management

```yaml
# Check paper usage
sensor_value: "{{ states('sensor.thermal_printer_paper_usage_percent') }}%"

# Reset paper usage counters
- action: esphome.thermal_printer_thermal_printer_reset_paper_usage

# Set custom roll length (in mm)
- action: esphome.thermal_printer_thermal_printer_set_paper_roll_length
  data:
    length_mm: 50000  # 50 meter roll
```

## Available Actions

Action names below are the suffix after `esphome.<device_name>_<printer_name>_`.

| Action | Fields (type) | Description |
|--------|---------------|-------------|
| `print_text` | `message` (string), `text_size` (string: `S`/`M`/`L`), `alignment` (string: `L`/`C`/`R`), `bold` (bool), `underline` (bool), `inverse` (bool) | Queue formatted text |
| `print_barcode` | `barcode_type` (int, see table below), `barcode_data` (string) | Queue a barcode |
| `print_qr_code` | `data` (string), `size` (int), `error_correction` (int), `label` (string, `""` for none) | Queue a QR code with optional label |
| `print_two_column` | `left_text` (string), `right_text` (string), `fill_dots` (bool), `text_size` (string: `S`/`M`/`L`) | Queue a two-column line with optional dot leaders |
| `print_table_row` | `col1` (string), `col2` (string), `col3` (string, `""` for two columns), `header` (bool) | Queue a table row |
| `feed_paper` | `lines` (int) | Queue a paper feed |
| `clear_queue` | - | Drop all pending jobs |
| `process_queue` | - | Process the queue now |
| `flush_queue` | - | Process the queue and wait until it is empty |
| `configure_queue` | `max_size` (int), `delay_ms` (int), `auto_process` (bool) | Change queue size, delay between jobs and auto-processing |
| `print_text_lines` | `lines` (string), `separator` (string, `""` = newline) | Queue each line as small left-aligned text |
| `print_receipt` | `business_name` (string), `items` (string, one item per line), `total` (string), `add_date` (bool) | Queue a complete receipt |
| `wake_printer` | - | Wake printer immediately (bypasses queue) |
| `sleep_printer` | - | Put printer to sleep immediately (bypasses queue) |
| `test_print` | - | Print test page immediately (bypasses queue) |
| `reset_paper_usage` | - | Reset paper usage counters |
| `set_paper_roll_length` | `length_mm` (float) | Set paper roll length for usage calculation |

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
