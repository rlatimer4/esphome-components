# ESPHome Jura Coffee Machine Component

A modern ESPHome external component for integrating Jura coffee machines with Home Assistant. This component allows you to monitor coffee consumption statistics, tank/tray status, and control your Jura coffee machine remotely.

## Features

- 📊 **Coffee Statistics**: Track single espresso, double espresso, coffee, and double coffee counts
- 🧽 **Maintenance Tracking**: Monitor cleaning cycles performed
- 💧 **Status Monitoring**: Water tank and drip tray status detection
- 🎛️ **Remote Control**: Virtual buttons for machine operation
- 🏠 **Home Assistant Integration**: Seamlessly integrates as native HA entities
- ⚙️ **Configurable**: Flexible sensor configuration and timeout settings
- 🔧 **Modern Architecture**: Uses ESPHome's latest external components system

## Compatible Devices

This component has been tested with various Jura coffee machines including:
- Jura E8
- Jura Z10
- Jura S8
- Other Jura models with serial interface support

> **Note**: Your Jura machine must have a service port (usually a 4-pin connector) for UART communication.

## Hardware Requirements

- ESP32 development board
- TTL to RS232 converter (or direct connection if your Jura uses TTL levels)
- Jura service cable or custom connector
- Optional: Status LED on GPIO2

## Installation

### Step 1: Download the Component

1. Create a `jura` folder in your ESPHome configuration directory
2. Download and place these files in the `jura` folder:
   - `__init__.py`
   - `jura.h`

Your directory structure should look like:
```
your_esphome_config/
├── your_device.yaml
└── jura/
    ├── __init__.py
    └── jura.h
```

### Step 2: Hardware Connection

Connect your ESP32 to the Jura machine:
- ESP32 GPIO17 (TX) → Jura RX
- ESP32 GPIO16 (RX) → Jura TX
- Ground connection between ESP32 and Jura
- Power the ESP32 via USB or external supply

### Step 3: ESPHome Configuration

Create or modify your ESPHome YAML configuration:

```yaml
substitutions:
  devicename: jura_coffee
  friendly_name: Coffee Machine
  device_description: Jura Coffee Machine in Kitchen

# Basic ESPHome configuration
esphome:
  name: ${devicename}

esp32:
  board: esp32dev
  framework:
    type: esp-idf

# WiFi and API setup
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

logger:

# External component configuration
external_components:
  - source:
      type: local
      path: jura

# UART configuration for Jura communication
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600
  id: uart_bus

# Jura component configuration
jura:
  uart_id: uart_bus
  update_interval: 60s
  timeout_ms: 5000  # Optional: communication timeout in milliseconds
  
  # Configure the sensors you want (all optional)
  single_espresso:
    id: num_single_espresso
    name: "Single Espressos Made"
  double_espresso:
    id: num_double_espresso
    name: "Double Espressos Made"
  coffee:
    id: num_coffee
    name: "Coffees Made"
  double_coffee:
    id: num_double_coffee
    name: "Double Coffees Made"
  cleanings:
    id: num_clean
    name: "Cleanings Performed"
  tray_status:
    id: tray_status
    name: "Drip Tray Status"
  tank_status:
    id: tank_status
    name: "Water Tank Status"
```

## Configuration Options

### Component Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `uart_id` | string | required | ID of the UART component to use |
| `update_interval` | time | `60s` | How often to poll the machine |
| `timeout_ms` | int | `5000` | Communication timeout in milliseconds |

### Available Sensors

| Sensor | Type | Description | Icon |
|--------|------|-------------|------|
| `single_espresso` | sensor | Count of single espressos made | `mdi:counter` |
| `double_espresso` | sensor | Count of double espressos made | `mdi:counter` |
| `coffee` | sensor | Count of regular coffees made | `mdi:counter` |
| `double_coffee` | sensor | Count of double coffees made | `mdi:counter` |
| `cleanings` | sensor | Number of cleaning cycles performed | `mdi:spray-bottle` |
| `tray_status` | text_sensor | Drip tray status ("OK" or "Not Fitted") | `mdi:tray` |
| `tank_status` | text_sensor | Water tank status ("OK" or "Fill Tank") | `mdi:cup-water` |

### Minimal Configuration

You can configure only the sensors you need:

```yaml
jura:
  uart_id: uart_bus
  single_espresso:
    name: "Espresso Count"
  tank_status:
    name: "Water Tank"
```

## Troubleshooting

### Common Issues

**1. No sensor data / all sensors show 0:**
- Check UART wiring (TX/RX may be swapped)
- Verify baud rate is 9600
- Check if your Jura model uses different commands
- Enable verbose logging to see raw communication

**2. "Timeout waiting for response" errors:**
- Increase `timeout_ms` value
- Check physical connections
- Ensure your Jura machine is powered on
- Some machines may need different timing

**3. Incorrect sensor values:**
- Different Jura models may use different response formats
- Check the parsing positions in the code
- Enable debug logging to see raw responses

### Debugging

Enable detailed logging in your ESPHome configuration:

```yaml
logger:
  level: VERBOSE
  logs:
    jura: VERBOSE
```

This will show all communication between the ESP32 and your coffee machine.

### Testing Communication

You can test basic communication by sending raw UART commands:

```yaml
button:
  - platform: template
    name: "Test Communication"
    on_press:
      - uart.write: "RT:0000\r\n"
```

## Home Assistant Integration

Once configured and connected, your coffee machine will appear in Home Assistant with:

- **Sensors**: Coffee counters and status indicators
- **Switches**: Machine power control
- **Buttons**: Brew commands and navigation

### Example Automations

**Low water notification:**
```yaml
automation:
  - alias: "Coffee machine needs water"
    trigger:
      platform: state
      entity_id: text_sensor.coffee_machine_water_tank_status
      to: "Fill Tank"
    action:
      service: notify.mobile_app
      data:
        message: "Coffee machine water tank needs refilling!"
```

**Daily coffee consumption:**
```yaml
template:
  - sensor:
      - name: "Today's Coffee Count"
        state: >
          {{ states('sensor.single_espressos_made')|int + 
             states('sensor.double_espressos_made')|int + 
             states('sensor.coffees_made')|int + 
             states('sensor.double_coffees_made')|int }}
```

## What's New in This Version

### Improvements Made:

1. **Enhanced Error Handling**
   - Bounds checking before string operations
   - Try-catch blocks around parsing logic
   - Graceful handling of malformed responses

2. **Configurable Timeouts**
   - `timeout_ms` parameter instead of hardcoded values
   - Adjustable for different Jura models and network conditions

3. **Better Debugging**
   - Verbose logging for communication debugging
   - Enhanced error messages with context
   - Configuration logging shows active sensors

4. **Input Validation**
   - Ensures at least one sensor is configured
   - Prevents misconfigurations at compile time

5. **Production Features**
   - Better Home Assistant integration
   - Template sensor for total coffee count
   - Enhanced display logic with initialization states
   - Fallback WiFi hotspot configuration

## Advanced Configuration

### Custom Display Logic

The component includes globals for managing display state:

```yaml
globals:
  - id: current_tab
    type: int
    restore_value: no
    initial_value: '1'
  - id: current_screen
    type: std::string
    restore_value: no
    initial_value: '"Menu"'

text_sensor:
  - platform: template
    id: display_current
    name: "Current Display"
    lambda: |-
      std::string tank = id(tank_status).state;
      std::string tray = id(tray_status).state;
      if (tank != "OK") {
        return {"Fill_Tank"}; 
      } else if (tray != "OK") {
        return {"Tray_Not_Fitted"};
      } else {
        return id(current_screen) + " " + std::to_string(id(current_tab));
      }
```

## Contributing

Contributions are welcome! Please:

1. Test with your Jura model
2. Document any model-specific findings
3. Submit pull requests with improvements
4. Report issues with detailed logs

## Credits

- Original component by [ryanalden](https://github.com/ryanalden/esphome-jura-component)
- Modernized for ESPHome external components architecture
- Jura communication protocol reverse engineering by the community

## License

This project is licensed under the MIT License - see the original repository for details.

## Disclaimer

This component is not officially supported by Jura. Use at your own risk. Modifying your coffee machine may void the warranty.
