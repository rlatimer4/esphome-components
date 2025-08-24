#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <string>

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
  // Sensor setters
  void set_single_espresso_sensor(sensor::Sensor *s) { this->single_espresso_sensor_ = s; }
  void set_tank_status_sensor(text_sensor::TextSensor *s) { this->tank_status_sensor_ = s; }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Test component with sensors...");
    if (this->single_espresso_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Single espresso sensor configured");
    if (this->tank_status_sensor_ != nullptr) ESP_LOGCONFIG(TAG, "  Tank status sensor configured");
  }

  void update() override {
    ESP_LOGI(TAG, "Testing Jura communication with sensor publishing...");
    
    // Test counter data parsing and publishing
    std::string result = cmd2jura("RT:0000");
    
    if (result.length() >= 39) {
        ESP_LOGI(TAG, "Counter data received (%d chars): %s", result.length(), result.c_str());
        
        // Parse exactly like before
        long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
        long num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
        long num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
        long num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
        long num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);

        ESP_LOGI(TAG, "Parsed counters - Single: %ld, Double: %ld, Coffee: %ld, Double Coffee: %ld, Cleanings: %ld", 
                 num_single_espresso, num_double_espresso, num_coffee, num_double_coffee, num_clean);

        // NOW TRY PUBLISHING - this is the key test
        if (this->single_espresso_sensor_ != nullptr) {
            ESP_LOGI(TAG, "Publishing single espresso count: %ld", num_single_espresso);
            this->single_espresso_sensor_->publish_state(num_single_espresso);
            ESP_LOGI(TAG, "Successfully published single espresso sensor");
        }
    } else {
        ESP_LOGW(TAG, "Counter data too short (%d chars): %s", result.length(), result.c_str());
    }

    // Test status data parsing and publishing
    result = cmd2jura("IC:");
    
    if (result.length() >= 5) {
        ESP_LOGI(TAG, "Status data received (%d chars): %s", result.length(), result.c_str());
        
        // Parse exactly like before  
        uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
        int trayBit = bitRead(hex_to_byte, 4);
        int tankBit = bitRead(hex_to_byte, 5);
        
        ESP_LOGI(TAG, "Status byte: 0x%02X, tray bit: %d, tank bit: %d", hex_to_byte, trayBit, tankBit);
        
        std::string tray_status = (trayBit == 1) ? "Not Fitted" : "OK";
        std::string tank_status = (tankBit == 1) ? "Fill Tank" : "OK";

        ESP_LOGI(TAG, "Status - Tray: %s, Tank: %s", tray_status.c_str(), tank_status.c_str());

        // NOW TRY PUBLISHING - this is the key test
        if (this->tank_status_sensor_ != nullptr) {
            ESP_LOGI(TAG, "Publishing tank status: %s", tank_status.c_str());
            this->tank_status_sensor_->publish_state(tank_status);
            ESP_LOGI(TAG, "Successfully published tank status sensor");
        }
    } else {
        ESP_LOGW(TAG, "Status data too short (%d chars): %s", result.length(), result.c_str());
    }
  }

 protected:
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    uint32_t timeout_loops = 500;
    uint32_t w = 0;

    ESP_LOGV(TAG, "Sending command: %s", outbytes.c_str());

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
      if (w++ > timeout_loops) {
        ESP_LOGW(TAG, "Timeout waiting for response after ~5 seconds. Partial response: '%s'", inbytes.c_str());
        return "";
      }
    }
    
    std::string response = inbytes.substr(0, inbytes.length() - 2);
    ESP_LOGV(TAG, "Received response: %s", response.c_str());
    return response;
  }

  // Sensor pointers
  sensor::Sensor *single_espresso_sensor_{nullptr};
  text_sensor::TextSensor *tank_status_sensor_{nullptr};
};

}  // namespace jura_test
}  // namespace esphome
