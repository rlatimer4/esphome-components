#include "binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer.binary_sensor";

void ThermalPrinterBinarySensor::setup() {
  this->parent_->set_paper_check_callback([this](bool has_paper) {
    this->publish_state(has_paper);
  });
}

void ThermalPrinterBinarySensor::loop() {
  // Check paper status every 10 seconds
  if (millis() - this->last_check_ > 10000) {
    this->last_check_ = millis();
    bool has_paper = this->parent_->has_paper();
    if (has_paper != this->last_state_) {
      this->last_state_ = has_paper;
      this->publish_state(has_paper);
    }
  }
}

}  // namespace thermal_printer
}  // namespace esphome
