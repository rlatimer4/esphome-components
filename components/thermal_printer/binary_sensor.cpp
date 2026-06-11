#include "binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const BINARY_SENSOR_TAG = "thermal_printer.binary_sensor";

void ThermalPrinterBinarySensor::setup() {
  if (this->parent_ == nullptr)
    return;

  // The parent component polls the printer; we just listen for changes
  this->parent_->add_paper_check_callback([this](bool has_paper) {
    this->publish_state(has_paper);
    ESP_LOGD(BINARY_SENSOR_TAG, "Paper loaded: %s", has_paper ? "YES" : "NO");
  });

  // Publish initial state from the parent's cached status
  this->publish_state(this->parent_->get_paper_status());
}

}  // namespace thermal_printer
}  // namespace esphome
