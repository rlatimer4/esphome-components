#include "binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer.binary_sensor";

void ThermalPrinterBinarySensor::setup() {
  // Set up callback for paper status changes
  if (this->parent_) {
    this->parent_->set_paper_check_callback([this](bool has_paper) {
      if (this->state != has_paper) {
        this->publish_state(has_paper);
        ESP_LOGD(TAG, "Paper loaded: %s", has_paper ? "YES" : "NO");
      }
    });
  }
  
  // Initial status check
  if (this->parent_) {
    bool has_paper = this->parent_->has_paper();
    this->last_state_ = has_paper;
    this->publish_state(has_paper);
  }
}

void ThermalPrinterBinarySensor::loop() {
  // Check paper status every 10 seconds
  if (millis() - this->last_check_ > 10000) {
    this->last_check_ = millis();
    
    if (this->parent_) {
      bool current_status = this->parent_->has_paper();
      if (current_status != this->last_state_) {
        this->last_state_ = current_status;
        this->publish_state(current_status);
        ESP_LOGD(TAG, "Paper status changed: %s", current_status ? "Loaded" : "Out");
      }
    }
  }
}

}  // namespace thermal_printer
}  // namespace esphome
