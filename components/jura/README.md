# ESPHome Jura Coffee Machine Component v2.0

A comprehensive ESPHome external component for integrating Jura coffee machines with Home Assistant, featuring optimistic screen tracking and a custom Lovelace control card.

## 🆕 What's New in v2.0

- **Optimistic Screen Tracking**: ESP32 tracks current screen state without tank/tray sensors
- **Context-Aware Buttons**: Button functions change based on current screen and brewing state
- **Brewing State Management**: Tracks strength/volume adjustment phases during brewing
- **Custom Lovelace Card**: Beautiful, responsive control interface for Home Assistant
- **Enhanced Error Handling**: Improved timeout handling and bounds checking
- **Model-Specific Adaptations**: Tailored for models without working tank/tray status

## Features

- 📊 **Coffee Statistics**: Track espresso, coffee, and cleaning counts
- 🎯 **Smart Screen Tracking**: Optimistic tracking of current display screen
- 🎛️ **Context-Aware Controls**: Button functions adapt to current screen
- ⏱️ **Brewing Management**: Handles strength and volume adjustment phases
- 🏠 **Home Assistant Integration**: Custom Lovelace card for beautiful control
- ⚙️ **Configurable Timeouts**: Adjustable communication settings
- 🔧 **Modern Architecture**: Uses ESPHome's latest external components system

## Compatible Devices

This component works with Jura coffee machines that have:
- Serial interface support (4-pin service connector)
- 6-button display layout (3 buttons per side)
- Standard Jura communication protocol

**Tested Models:**
- Jura E8 (original test model)
- Jura Z10 
- Jura S8
- Other models with similar button layouts

## Hardware Requirements

- ESP32 development board
- TTL to RS232 converter (if needed)
- Jura service cable or custom connector
- Optional: Status LED on GPIO2

## Installation

### Step 1: Download the Component

Create a `jura` folder in your ESPHome configuration directory and add these files:

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

Use the provided `jura_coffee.yaml` as your configuration template. Key features:

```yaml
# External component
external_components:
  - source:
      type: local
      path: jura

# Enhanced Jura component
jura:
  uart_id: uart_bus
  update_interval: 60s
  timeout_ms: 5000  # Configurable timeout
  
  # Only sensors that work (no tank/tray status)
  single_espresso:
    name: "Single Espressos Made"
  # ... other sensors
```

### Step 4: Custom Lovelace Card Installation

1. **Download the card file**: Save `jura-coffee-card.js` to your Home Assistant `www` folder:
   ```
   /config/www/jura-coffee-card.js
   ```

2. **Register the card**: Add to your `configuration.yaml`:
   ```yaml
   lovelace:
     mode: yaml
     resources:
       - url: /local/jura-coffee-card.js
         type: module
   ```

3. **Add to dashboard**: Use the provided Lovelace configuration or add via UI:
   ```yaml
   type: custom:jura-coffee-card
   entities:
     power: switch.coffee_machine_power
     current_screen: text_sensor.current_screen
     is_brewing: binary_sensor.is_brewing
     # ... see full config
   ```

## Screen Layout Understanding

Your Jura machine has two coffee screens and five menu screens:

### Coffee Screens
**Screen 1:**
```
Espresso    | Coffee
Ristretto   | Hot Water  
Menu        | Next
```

**Screen 2:**
```
Cappuccino  | Flat White
Latte Mac.  | 1P Milk
Menu        | Next
```

### Menu Screens (1-5)
Navigate through cleaning, maintenance, settings, and configuration options.

## Button Behavior

- **During Brewing** (first 4 seconds): Middle buttons control strength
- **During Brewing** (next 10 seconds): Middle buttons control volume
- **During Brewing**: Bottom left cancels brew
- **Normal Operation**: Buttons follow screen layout

## Configuration Options

### Jura Component

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `uart_id` | string | required | UART component ID |
| `update_interval` | time | `60s` | Polling frequency |
| `timeout_ms` | int | `5000` | Communication timeout |

### Available Sensors

| Sensor | Description | Works On Your Model |
|--------|-------------|-------------------|
| `single_espresso` | Single espresso count | ✅ Yes |
| `double_espresso` | Double espresso count | ✅ Yes |
| `coffee` | Regular coffee count | ✅ Yes |
| `double_coffee` | Double coffee count | ✅ Yes |
| `cleanings` | Cleaning cycles count | ✅ Yes |
| `tray_status` | Drip tray status | ❌ Not working |
| `tank_status` | Water tank status | ❌ Not working |

## Custom Card Features

- **Responsive Design**: Works on mobile and desktop
- **Real-time Updates**: Reflects current machine state
- **Context-Aware**: Button labels change based on screen
- **Brewing Animation**: Visual feedback during operation
- **Statistics Display**: Shows coffee consumption data
- **Connection Status**: Indicates Home Assistant connectivity

## Troubleshooting

### Common Issues

**No sensor data:**
- Check UART wiring (TX/RX may be swapped)  
- Verify baud rate is 9600
- Increase `timeout_ms` if needed

**Screen tracking incorrect:**
- Check button press sequences match your model
- Enable verbose logging to debug state changes

**Custom card not loading:**
- Verify file is in `/config/www/` folder
- Check browser console for JavaScript errors
- Ensure resource is registered in configuration.yaml

### Debugging

Enable detailed logging:

```yaml
logger:
  level: VERBOSE
  logs:
    jura: VERBOSE
```

## Home Assistant Automations

### Low Coffee Alert
```yaml
automation:
  - alias: "Daily coffee limit reached"
    trigger:
      platform: numeric_state
      entity_id: sensor.total_coffees_made_today
      above: 10
    action:
      service: notify.mobile_app
      data:
        message: "Maybe switch to decaf? ☕"
```

### Auto-power Off
```yaml
automation:
  - alias: "Coffee machine auto-off"
    trigger:
      platform: state
      entity_id: binary_sensor.is_brewing
      from: 'on'
      to: 'off'
      for: '02:00:00'
    action:
      service: switch.turn_off
      entity_id: switch.coffee_machine_power
```

## Advance
