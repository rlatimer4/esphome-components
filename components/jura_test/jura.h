#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include <string>

// Forward declarations - only include headers if sensors are actually configured
namespace esphome {
  namespace sensor {
    class Sensor;
  }
  namespace text_sensor {
    class TextSensor;
  }
}

namespace esphome {
namespace jura_test {

static const char *const TAG = "jura_test";

// Helper functions
static bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

class JuraCoffeeComponent : public PollingComponent, public uart::UARTDevice {
 public:
  // Optional sensor setters
  void set_single_espresso_sensor(esphome::sensor::Sensor *s) { this->single_espresso_sensor_ = s; }
  void set_tank_status_sensor(esphome::text_sensor::TextSensor *s) { this->tank_status_sensor_ = s; }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Test component...");
    // Very minimal setup
    if (this->single_espresso_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Single espresso sensor configured");
    if (this->tank_status_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Tank status sensor configured");
  }

  void update() override {
    ESP_LOGI(TAG, "Testing Jura communication...");
    
    // Test counter data if we have a sensor
    if (this->single_espresso_sensor_ != nullptr) {
      std::string result = cmd2jura("RT:0000");
      
      if (result.length() >= 39) {
          ESP_LOGI(TAG, "Counter data received: %s", result.c_str());
          long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
          ESP_LOGI(TAG, "Single espresso count: %ld", num_single_espresso);
          this->single_espresso_sensor_->publish_state(num_single_espresso);
      } else {
          ESP_LOGW(TAG, "Counter data too short or empty: %s", result.c_str());
      }
    }
    
    // Test status data if we have a sensor
    if (this->tank_status_sensor_ != nullptr) {
      std::string result = cmd2jura("IC:");
      
      if (result.length() >= 5) {
          ESP_LOGI(TAG, "Status data received: %s", result.c_str());
          uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
          int tankBit = bitRead(hex_to_byte, 5);
          std::string tank_status = (tankBit == 1) ? "Fill Tank" : "OK";
          ESP_LOGI(TAG, "Tank status: %s", tank_status.c_str());
          this->tank_status_sensor_->publish_state(tank_status);
      } else {
          ESP_LOGW(TAG, "Status data too short or empty: %s", result.c_str());
      }
    }
    
    // If no sensors configured, just test communication
    if (this->single_espresso_sensor_ == nullptr && this->tank_status_sensor_ == nullptr) {
      std::string result = cmd2jura("RT:0000");
      if (result.length() > 0) {
          ESP_LOGI(TAG, "Basic communication test SUCCESS: %s", result.c_str());
      } else {
          ESP_LOGW(TAG, "Basic communication test FAILED");
      }
    }
  }

 protected:
  // Minimal Jura communication function
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    int w = 0;

    // Clear receive buffer
    while (available()) {
      read();
    }

    outbytes += "\r\n";
    for (int i = 0; i < outbytes.length(); i++) {
      for (int s = 0; s < 8; s += 2) {
        uint8_t rawbyte = 255;
        bitWrite(rawbyte, 2, bitRead(outbytes.at(i), s + 0));
        bitWrite(rawbyte, 5, bitRead(outbytes.at(i), s + 1));
        write(rawbyte);
      }
      delay(8);
    }

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
      if (w++ > 500) { // 5 second timeout
        return "";
      }
    }
    
    return inbytes.substr(0, inbytes.length() - 2);
  }

  // Optional sensor pointers
  esphome::sensor::Sensor *single_espresso_sensor_{nullptr};
  esphome::text_sensor::TextSensor *tank_status_sensor_{nullptr};
};

}  // namespace jura_test
}  // namespace esphome
