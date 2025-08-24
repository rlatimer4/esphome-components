#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
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

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Test component...");
    ESP_LOGCONFIG(TAG, "This version tests data parsing without sensors");
  }

  void update() override {
    ESP_LOGI(TAG, "Testing Jura communication with parsing...");
    
    // Test counter data parsing (like the original component)
    std::string result = cmd2jura("RT:0000");
    
    if (result.length() >= 39) {
        ESP_LOGI(TAG, "Counter data received (%d chars): %s", result.length(), result.c_str());
        
        // Parse exactly like the original enhanced component
        long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
        long num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
        long num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
        long num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
        long num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);

        ESP_LOGI(TAG, "Parsed counters - Single: %ld, Double: %ld, Coffee: %ld, Double Coffee: %ld, Cleanings: %ld", 
                 num_single_espresso, num_double_espresso, num_coffee, num_double_coffee, num_clean);
    } else {
        ESP_LOGW(TAG, "Counter data too short (%d chars): %s", result.length(), result.c_str());
    }

    // Test status data parsing (like the original component)
    result = cmd2jura("IC:");
    
    if (result.length() >= 5) {
        ESP_LOGI(TAG, "Status data received (%d chars): %s", result.length(), result.c_str());
        
        // Parse exactly like the original enhanced component  
        uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
        int trayBit = bitRead(hex_to_byte, 4);
        int tankBit = bitRead(hex_to_byte, 5);
        
        ESP_LOGI(TAG, "Status byte: 0x%02X, tray bit: %d, tank bit: %d", hex_to_byte, trayBit, tankBit);
        
        std::string tray_status = (trayBit == 1) ? "Not Fitted" : "OK";
        std::string tank_status = (tankBit == 1) ? "Fill Tank" : "OK";

        ESP_LOGI(TAG, "Status - Tray: %s, Tank: %s", tray_status.c_str(), tank_status.c_str());
    } else {
        ESP_LOGW(TAG, "Status data too short (%d chars): %s", result.length(), result.c_str());
    }
  }

 protected:
  // Use configurable timeout like the enhanced version
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    uint32_t timeout_loops = 500; // 5 seconds like enhanced version
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
        ESP_LOGW(TAG, "Timeout waiting for response after ~5 seconds. Partial response: '%s'", inbytes.c_str());
        return "";
      }
    }
    
    std::string response = inbytes.substr(0, inbytes.length() - 2);
    ESP_LOGV(TAG, "Received response: %s", response.c_str());
    return response;
  }
};

}  // namespace jura_test
}  // namespace esphome
