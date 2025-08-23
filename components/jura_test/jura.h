#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"
#include <string>

namespace esphome {
namespace jura_test {

static const char *const TAG = "jura_test";

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

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Jura Test component...");
    // Minimal setup - don't do anything that might trigger the machine
  }

  void update() override {
    ESP_LOGI(TAG, "Testing Jura communication...");
    
    // Just test basic communication without parsing or storing results
    std::string result = cmd2jura("RT:0000");
    
    if (result.length() > 0) {
        ESP_LOGI(TAG, "SUCCESS: Received response (%d chars): %s", result.length(), result.c_str());
    } else {
        ESP_LOGW(TAG, "FAILED: No response from Jura machine");
    }
    
    // Test status command too
    result = cmd2jura("IC:");
    if (result.length() > 0) {
        ESP_LOGI(TAG, "Status response (%d chars): %s", result.length(), result.c_str());
    } else {
        ESP_LOGW(TAG, "No status response from Jura machine");
    }
  }

 protected:
  // Basic Jura communication function for testing
  std::string cmd2jura(std::string outbytes) {
    std::string inbytes;
    int w = 0;

    ESP_LOGD(TAG, "Sending: %s", outbytes.c_str());

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
        ESP_LOGW(TAG, "Timeout waiting for response");
        return "";
      }
    }
    
    std::string response = inbytes.substr(0, inbytes.length() - 2);
    ESP_LOGD(TAG, "Received: %s", response.c_str());
    return response;
  }
};

}  // namespace jura_test
}  // namespace esphome
