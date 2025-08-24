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
    ESP_LOGCONFIG(TAG, "Setting up Jura Test component for detailed debugging...");
  }

  void update() override {
    ESP_LOGI(TAG, "=== Starting Jura Communication Test ===");
    
    // Test 1: Counter data with detailed parsing
    ESP_LOGI(TAG, "Test 1: Requesting counter data (RT:0000)...");
    std::string result = cmd2jura("RT:0000");
    
    if (result.length() >= 39) {
        ESP_LOGI(TAG, "✓ Counter data SUCCESS (%d chars): %s", result.length(), result.c_str());
        
        // Parse and log each value
        long num_single_espresso = strtol(result.substr(3,4).c_str(), nullptr, 16);
        long num_double_espresso = strtol(result.substr(7,4).c_str(), nullptr, 16);
        long num_coffee = strtol(result.substr(11,4).c_str(), nullptr, 16);
        long num_double_coffee = strtol(result.substr(15,4).c_str(), nullptr, 16);
        long num_clean = strtol(result.substr(35,4).c_str(), nullptr, 16);
        
        ESP_LOGI(TAG, "  Single Espresso: %ld", num_single_espresso);
        ESP_LOGI(TAG, "  Double Espresso: %ld", num_double_espresso);
        ESP_LOGI(TAG, "  Coffee: %ld", num_coffee);
        ESP_LOGI(TAG, "  Double Coffee: %ld", num_double_coffee);
        ESP_LOGI(TAG, "  Cleanings: %ld", num_clean);
        
    } else {
        ESP_LOGW(TAG, "✗ Counter data FAILED - too short (%d chars): '%s'", result.length(), result.c_str());
    }
    
    // Small delay between commands
    delay(100);
    
    // Test 2: Status data with detailed parsing  
    ESP_LOGI(TAG, "Test 2: Requesting status data (IC:)...");
    result = cmd2jura("IC:");
    
    if (result.length() >= 5) {
        ESP_LOGI(TAG, "✓ Status data SUCCESS (%d chars): %s", result.length(), result.c_str());
        
        // Parse status byte
        uint8_t hex_to_byte = strtol(result.substr(3,2).c_str(), nullptr, 16);
        int trayBit = bitRead(hex_to_byte, 4);
        int tankBit = bitRead(hex_to_byte, 5);
        
        ESP_LOGI(TAG, "  Status byte: 0x%02X", hex_to_byte);
        ESP_LOGI(TAG, "  Tray bit (4): %d -> %s", trayBit, (trayBit == 1) ? "Not Fitted" : "OK");
        ESP_LOGI(TAG, "  Tank bit (5): %d -> %s", tankBit, (tankBit == 1) ? "Fill Tank" : "OK");
        
    } else {
        ESP_LOGW(TAG, "✗ Status data FAILED - too short (%d chars): '%s'", result.length(), result.c_str());
    }
    
    ESP_LOGI(TAG, "=== Communication Test Complete ===");
  }

 protected:
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    int w = 0;

    ESP_LOGD(TAG, "Sending command: '%s'", outbytes.c_str());

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
        ESP_LOGW(TAG, "Timeout after ~5 seconds, partial response: '%s'", inbytes.c_str());
        return "";
      }
    }
    
    std::string response = inbytes.substr(0, inbytes.length() - 2);
    ESP_LOGD(TAG, "Received response: '%s'", response.c_str());
    return response;
  }
};

}  // namespace jura_test
}  // namespace esphome
