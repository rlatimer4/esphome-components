#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include <string>

namespace esphome {
namespace jura {

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
    // MINIMAL SETUP - exactly like original to avoid triggering protection
    ESP_LOGCONFIG(TAG, "Setting up Jura Coffee Machine component...");
  }

  void update() override {
    ESP_LOGD(TAG, "Polling Jura Coffee Machine for data...");
    std::string result;

    // --- Fetch and parse counter data ---
    result = cmd2jura("RT:0000");
    
    // Keep the enhanced bounds checking - this was good
    if (result.length() < 39) {
        ESP_LOGW(TAG, "Failed to get counter data or response was too short. Received: %s", result.c_str());
    } else {
        ESP_LOGD(TAG, "Received counter data: %s", result.c_str());
        
        // Keep the enhanced parsing
        long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
        long num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
        long num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
        long num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
        long num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);

        // Publish without verbose logging
        if (this->single_espresso_sensor_ != nullptr) this->single_espresso_sensor_->publish_state(num_single_espresso);
        if (this->double_espresso_sensor_ != nullptr) this->double_espresso_sensor_->publish_state(num_double_espresso);
        if (this->coffee_sensor_ != nullptr) this->coffee_sensor_->publish_state(num_coffee);
        if (this->double_coffee_sensor_ != nullptr) this->double_coffee_sensor_->publish_state(num_double_coffee);
        if (this->cleanings_sensor_ != nullptr) this->cleanings_sensor_->publish_state(num_clean);
    }

    // --- Fetch and parse status data ---
    result = cmd2jura("IC:");
    
    // Keep the enhanced bounds checking
    if (result.length() < 5) {
        ESP_LOGW(TAG, "Failed to get status data or response was too short. Received: %s", result.c_str());
    } else {
        ESP_LOGD(TAG, "Received status data: %s", result.c_str());
        
        // Keep the enhanced parsing
        uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
        int trayBit = bitRead(hex_to_byte, 4);
        int tankBit = bitRead(hex_to_byte, 5);
        std::string tray_status = (trayBit == 1) ? "Not Fitted" : "OK";
        std::string tank_status = (tankBit == 1) ? "Fill Tank" : "OK";

        if (this->tray_status_sensor_ != nullptr) this->tray_status_sensor_->publish_state(tray_status);
        if (this->tank_status_sensor_ != nullptr) this->tank_status_sensor_->publish_state(tank_status);
    }
  }

 protected:
  // Enhanced timeout but NO COMMUNICATION LOGGING to avoid interference
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    uint32_t timeout_loops = this->timeout_ms_ / 10; // 10ms per loop iteration
    uint32_t w = 0;

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
        // Only log timeout, not normal communication
        ESP_LOGW(TAG, "Timeout waiting for response after %d ms", this->timeout_ms_);
        return "";
      }
    }
    
    return inbytes.substr(0, inbytes.length() - 2);
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
