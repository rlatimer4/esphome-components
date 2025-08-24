#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include <string>

namespace esphome {
namespace jura {

static const char *const TAG = "jura";

// Helper functions for Jura protocol
static bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

class JuraCoffeeComponent : public PollingComponent, public uart::UARTDevice {
 public:
  // Sensor setters
  void set_single_espresso_sensor(sensor::Sensor *s) { this->single_espresso_sensor_ = s; }
  void set_double_espresso_sensor(sensor::Sensor *s) { this->double_espresso_sensor_ = s; }
  void set_coffee_sensor(sensor::Sensor *s) { this->coffee_sensor_ = s; }
  void set_double_coffee_sensor(sensor::Sensor *s) { this->double_coffee_sensor_ = s; }
  void set_cleanings_sensor(sensor::Sensor *s) { this->cleanings_sensor_ = s; }
  
  // Timeout setter
  void set_timeout_ms(int timeout) { this->timeout_ms_ = timeout; }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Coffee Machine component...");
    ESP_LOGCONFIG(TAG, "  Timeout: %d ms", this->timeout_ms_);
    
    // Log configured sensors
    if (this->single_espresso_sensor_) ESP_LOGCONFIG(TAG, "  Single Espresso sensor configured");
    if (this->double_espresso_sensor_) ESP_LOGCONFIG(TAG, "  Double Espresso sensor configured");
    if (this->coffee_sensor_) ESP_LOGCONFIG(TAG, "  Coffee sensor configured");
    if (this->double_coffee_sensor_) ESP_LOGCONFIG(TAG, "  Double Coffee sensor configured");
    if (this->cleanings_sensor_) ESP_LOGCONFIG(TAG, "  Cleanings sensor configured");

#ifdef JURA_NO_SENSORS_WARNING
    ESP_LOGW(TAG, "No sensors configured - component will poll but not publish data");
#endif
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Jura Coffee Machine:");
    ESP_LOGCONFIG(TAG, "  Update interval: %ums", this->get_update_interval());
    ESP_LOGCONFIG(TAG, "  Timeout: %d ms", this->timeout_ms_);
    LOG_SENSOR("  ", "Single Espresso", this->single_espresso_sensor_);
    LOG_SENSOR("  ", "Double Espresso", this->double_espresso_sensor_);
    LOG_SENSOR("  ", "Coffee", this->coffee_sensor_);
    LOG_SENSOR("  ", "Double Coffee", this->double_coffee_sensor_);
    LOG_SENSOR("  ", "Cleanings", this->cleanings_sensor_);
  }

  void update() override {
    ESP_LOGD(TAG, "Polling Jura Coffee Machine for data...");
    
    // Get counter data with enhanced error handling
    std::string result = cmd2jura("RT:0000");
    ESP_LOGD(TAG, "Received counter data: %s", result.c_str());
    
    if (result.empty()) {
      ESP_LOGW(TAG, "No response from coffee machine - check connection");
      return;
    }
    
    if (result.length() >= 39) {
      try {
        // Parse counter data with bounds checking
        long num_single_espresso = 0, num_double_espresso = 0;
        long num_coffee = 0, num_double_coffee = 0, num_clean = 0;
        
        if (result.length() > 7) {
          num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
          num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
        }
        if (result.length() > 19) {
          num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
          num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
        }
        if (result.length() > 39) {
          num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);
        }

        // Publish to sensors with validation
        if (this->single_espresso_sensor_ && num_single_espresso >= 0) {
          this->single_espresso_sensor_->publish_state(num_single_espresso);
        }
        if (this->double_espresso_sensor_ && num_double_espresso >= 0) {
          this->double_espresso_sensor_->publish_state(num_double_espresso);
        }
        if (this->coffee_sensor_ && num_coffee >= 0) {
          this->coffee_sensor_->publish_state(num_coffee);
        }
        if (this->double_coffee_sensor_ && num_double_coffee >= 0) {
          this->double_coffee_sensor_->publish_state(num_double_coffee);
        }
        if (this->cleanings_sensor_ && num_clean >= 0) {
          this->cleanings_sensor_->publish_state(num_clean);
        }
        
        ESP_LOGD(TAG, "Coffee counters - Single: %ld, Double: %ld, Coffee: %ld, Double Coffee: %ld, Clean: %ld", 
                 num_single_espresso, num_double_espresso, num_coffee, num_double_coffee, num_clean);
                 
      } catch (const std::exception& e) {
        ESP_LOGW(TAG, "Error parsing counter data: %s", e.what());
      }
    } else {
      ESP_LOGW(TAG, "Counter response too short: %zu chars (expected >= 39)", result.length());
    }
  }

 protected:
  // Enhanced cmd2jura function with configurable timeout and better error handling
  std::string cmd2jura(const std::string& outbytes) {
    std::string inbytes;
    int wait_count = 0;
    const int max_wait = this->timeout_ms_ / 10; // Convert ms to 10ms intervals

    // Clear any pending data
    while (available()) {
      read();
    }

    // Send command
    std::string full_command = outbytes + "\r\n";
    for (size_t i = 0; i < full_command.length(); i++) {
      for (int s = 0; s < 8; s += 2) {
        uint8_t rawbyte = 0xFF;
        bitWrite(rawbyte, 2, bitRead(full_command[i], s + 0));
        bitWrite(rawbyte, 5, bitRead(full_command[i], s + 1));
        write(rawbyte);
      }
      delay(8);
    }

    // Read response
    int bit_pos = 0;
    uint8_t current_byte = 0;
    
    while (!endsWith(inbytes, "\r\n")) {
      if (available()) {
        uint8_t rawbyte = read();
        bitWrite(current_byte, bit_pos + 0, bitRead(rawbyte, 2));
        bitWrite(current_byte, bit_pos + 1, bitRead(rawbyte, 5));
        
        bit_pos += 2;
        if (bit_pos >= 8) {
          bit_pos = 0;
          inbytes += static_cast<char>(current_byte);
          current_byte = 0;
        }
      } else {
        delay(10);
        wait_count++;
        if (wait_count > max_wait) {
          ESP_LOGW(TAG, "Timeout waiting for response to command: %s", outbytes.c_str());
          return "";
        }
      }
    }
    
    // Remove \r\n from response
    if (inbytes.length() >= 2) {
      return inbytes.substr(0, inbytes.length() - 2);
    }
    return inbytes;
  }

  // Configuration
  int timeout_ms_{5000};

  // Sensor pointers
  sensor::Sensor *single_espresso_sensor_{nullptr};
  sensor::Sensor *double_espresso_sensor_{nullptr};
  sensor::Sensor *coffee_sensor_{nullptr};
  sensor::Sensor *double_coffee_sensor_{nullptr};
  sensor::Sensor *cleanings_sensor_{nullptr};
};

}  // namespace jura
}  // namespace esphome
