#include "text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer.text_sensor";

void ThermalPrinterTextSensor::setup() {
  this->parent_->set_paper_check_callback([this](bool has_paper) {
    this->publish_state(has_paper ? "Present" : "Out");
  });
}

void ThermalPrinterTextSensor::loop() {
  // Check paper status every 10 seconds
  if (millis() - this->last_check_ > 10000) {
    this->last_check_ = millis();
    bool has_paper = this->parent_->has_paper();
    this->publish_state(has_paper ? "Present" : "Out");
  }
}

}  // namespace thermal_printer
}  // namespace esphome
