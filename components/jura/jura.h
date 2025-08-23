#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h" // Required for logging
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include <string>

namespace esphome {
namespace jura {

// A tag for our log messages, so they are easy to identify.
static const char *const TAG = "jura";

// Helper functions from your original code
static bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

class JuraCoffeeComponent : public PollingComponent, public uart::UARTDevice {
 public:
  // Setters for each sensor that can be configured in YAML
  void set_single_espresso_sensor(sensor::Sensor *s) { this->single_espresso_sensor_ = s; }
  void set_double_espresso_sensor(sensor::Sensor *s) { this->double_espresso_sensor_ = s; }
  void set_coffee_sensor(sensor::Sensor *s) { this->coffee_sensor_ = s; }
  void set_double_coffee_sensor(sensor::Sensor *s) { this->double_coffee_sensor_ = s; }
  void set_cleanings_sensor(sensor::Sensor *s) { this->cleanings_sensor_ = s; }
  void set_tray_status_sensor(text_sensor::TextSensor *s) { this->tray_status_sensor_ = s; }
  void set_tank_status_sensor(text_sensor::TextSensor *s) { this->tank_status_sensor_ = s; }
  void set_timeout_ms(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Coffee Machine component...");
    ESP_LOGCONFIG(TAG, "  Timeout: %d ms", this->timeout_ms_);
    ESP_LOGCONFIG(TAG, "  Update interval: %d ms", this->get_update_interval());
    
    // Log which sensors are configured
    if (this->single_espresso_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Single Espresso sensor configured");
    if (this->double_espresso_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Double Espresso sensor configured");
    if (this->coffee_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Coffee sensor configured");
    if (this->double_coffee_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Double Coffee sensor configured");
    if (this->cleanings_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Cleanings sensor configured");
    if (this->tray_status_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Tray Status sensor configured");
    if (this->tank_status_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Tank Status sensor configured");
  }

  void update() override {
    ESP_LOGD(TAG, "Polling Jura Coffee Machine for data...");
    std::string result;

    // --- Fetch and parse counter data ---
    ESP_LOGV(TAG, "Requesting counter data...");
    result = cmd2jura("RT:0000");
    
    // ROBUSTNESS: Check if the response is valid before trying to parse it.
    if (result.length() < 39) { // 35 characters for cleanings + 4 for the value
        ESP_LOGW(TAG, "Failed to get counter data or response was too short. Expected >=39 chars, got %d. Received: '%s'", result.length(), result.c_str());
    } else {
        ESP_LOGD(TAG, "Received counter data (%d chars): %s", result.length(), result.c_str());
        
        // Parse the data with bounds checking
        try {
            long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
            long num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
            long num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
            long num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
            long num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);

            ESP_LOGV(TAG, "Parsed counters - Single: %ld, Double: %ld, Coffee: %ld, Double Coffee: %ld, Cleanings: %ld", 
                     num_single_espresso, num_double_espresso, num_coffee, num_double_coffee, num_clean);

            if (this->single_espresso_sensor_ != nullptr) {
                this->single_espresso_sensor_->publish_state(num_single_espresso);
                ESP_LOGV(TAG, "Published single espresso: %ld", num_single_espresso);
            }
            if (this->double_espresso_sensor_ != nullptr) {
                this->double_espresso_sensor_->publish_state(num_double_espresso);
                ESP_LOGV(TAG, "Published double espresso: %ld", num_double_espresso);
            }
            if (this->coffee_sensor_ != nullptr) {
                this->coffee_sensor_->publish_state(num_coffee);
                ESP_LOGV(TAG, "Published coffee: %ld", num_coffee);
            }
            if (this->double_coffee_sensor_ != nullptr) {
                this->double_coffee_sensor_->publish_state(num_double_coffee);
                ESP_LOGV(TAG, "Published double coffee: %ld", num_double_coffee);
            }
            if (this->cleanings_sensor_ != nullptr) {
                this->cleanings_sensor_->publish_state(num_clean);
                ESP_LOGV(TAG, "Published cleanings: %ld", num_clean);
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error parsing counter data: %s", e.what());
        }
    }

    // --- Fetch and parse status data ---
    ESP_LOGV(TAG, "Requesting status data...");
    result = cmd2jura("IC:");
    
    // ROBUSTNESS: Check if the response is valid before trying to parse it.
    if (result.length() < 5) { // 3 characters for "IC:" + 2 for the value
        ESP_LOGW(TAG, "Failed to get status data or response was too short. Expected >=5 chars, got %d. Received: '%s'", result.length(), result.c_str());
    } else {
        ESP_LOGD(TAG, "Received status data (%d chars): %s", result.length(), result.c_str());
        
        try {
            // Parse the data with bounds checking
            uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
            int trayBit = bitRead(hex_to_byte, 4);
            int tankBit = bitRead(hex_to_byte, 5);
            
            ESP_LOGV(TAG, "Status byte: 0x%02X, tray bit: %d, tank bit: %d", hex_to_byte, trayBit, tankBit);
            
            std::string tray_status = (trayBit == 1) ? "Not Fitted" : "OK";
            std::string tank_status = (tankBit == 1) ? "Fill Tank" : "OK";

            if (this->tray_status_sensor_ != nullptr) {
                this->tray_status_sensor_->publish_state(tray_status);
                ESP_LOGV(TAG, "Published tray status: %s", tray_status.c_str());
            }
            if (this->tank_status_sensor_ != nullptr) {
                this->tank_status_sensor_->publish_state(tank_status);
                ESP_LOGV(TAG, "Published tank status: %s", tank_status.c_str());
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error parsing status data: %s", e.what());
        }
    }
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Jura Coffee Machine:");
    ESP_LOGCONFIG(TAG, "  Timeout: %d ms", this->timeout_ms_);
    ESP_LOGCONFIG(TAG, "  Update Interval: %d ms", this->get_update_interval());
  }

 protected:
  // Jura communication function with configurable timeout and enhanced logging
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    uint32_t timeout_loops = this->timeout_ms_ / 10; // 10ms per loop iteration
    uint32_t w = 0;

    ESP_LOGV(TAG, "Sending command: %s", outbytes.c_str());

    // Clear any pending data in the receive buffer
    while (available()) {
      read();
    }

    outbytes += "\r\n";
    
    // Send command byte by byte with Jura encoding
    for (int i = 0; i < outbytes.length(); i++) {
      for (int s = 0; s < 8; s += 2) {
        uint8_t rawbyte = 255;
        bitWrite(rawbyte, 2, bitRead(outbytes.at(i), s + 0));
        bitWrite(rawbyte, 5, bitRead(outbytes.at(i), s + 1));
        write(rawbyte);
      }
      delay(8);
    }

    // Read response with Jura decoding
    int s = 0;
    uint8_t inbyte = 0;
    while (!endsWith(inbytes, "\r\n")) {
      if (available()) {
        uint8_t rawbyte = read();
        bitWrite(inbyte, s + 0, bitRead(rawbyte, 2));
        bitWrite(inbyte, s + 1, bitRead(rawbyte, 5));
        if ((s += 2) >= 8) {
          s = 0;
          inbytes += inbyte;
        }
      } else {
        delay(10);
      }
      if (w++ > timeout_loops) {
        ESP_LOGW(TAG, "Timeout waiting for response after %d ms. Partial response: '%s'", this->timeout_ms_, inbytes.c_str());
        return "";
      }
    }
    
    std::string response = inbytes.substr(0, inbytes.length() - 2);
    ESP_LOGV(TAG, "Received response: %s", response.c_str());
    return response;
  }

  // Configuration
  uint32_t timeout_ms_ = 5000; // Default 5 second timeout

  // Pointers for the sensors
  sensor::Sensor *single_espresso_sensor_{nullptr};
  sensor::Sensor *double_espresso_sensor_{nullptr};
  sensor::Sensor *coffee_sensor_{nullptr};
  sensor::Sensor *double_coffee_sensor_{nullptr};
  sensor::Sensor *cleanings_sensor_{nullptr};
  text_sensor::TextSensor *tray_status_sensor_{nullptr};
  text_sensor::TextSensor *tank_status_sensor_{nullptr};
};

}  // namespace jura
}  // namespace esphome
