#include "text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer.text_sensor";

void ThermalPrinterTextSensor::setup() {
  // Set up callback for paper status changes
  if (this->parent_) {
    this->parent_->set_paper_check_callback([this](bool has_paper) {
      std::string status = has_paper ? "Present" : "Out";
      if (this->state != status) {
        this->publish_state(status);
        ESP_LOGD(TAG, "Paper status: %s", status.c_str());
      }
    });
  }
  
  // Initial status check
  if (this->parent_) {
    bool has_paper = this->parent_->has_paper();
    this->last_paper_status_ = has_paper;
    this->publish_state(has_paper ? "Present" : "Out");
  }
}

void ThermalPrinterTextSensor::loop() {
  // Check paper status every 10 seconds
  if (millis() - this->last_check_ > 10000) {
    this->last_check_ = millis();
    
    if (this->parent_) {
      bool current_status = this->parent_->has_paper();
      if (current_status != this->last_paper_status_) {
        this->last_paper_status_ = current_status;
        std::string status = current_status ? "Present" : "Out";
        this->publish_state(status);
        ESP_LOGD(TAG, "Paper status changed: %s", status.c_str());
      }
    }
  }
}

}  // namespace thermal_printer
}  // namespace esphome
