#include "text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TEXT_SENSOR_TAG = "thermal_printer.text_sensor";

void ThermalPrinterTextSensor::setup() {
  if (this->parent_ == nullptr)
    return;

  // The parent component polls the printer; we just listen for changes
  this->parent_->add_paper_check_callback([this](bool has_paper) {
    std::string status = has_paper ? "Present" : "Out";
    this->publish_state(status);
    ESP_LOGD(TEXT_SENSOR_TAG, "Paper status: %s", status.c_str());
  });

  // Publish initial state from the parent's cached status
  this->publish_state(this->parent_->get_paper_status() ? "Present" : "Out");
}

}  // namespace thermal_printer
}  // namespace esphome
