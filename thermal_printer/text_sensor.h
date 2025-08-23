#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "thermal_printer.h"

namespace esphome {
namespace thermal_printer {

class ThermalPrinterTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void set_parent(ThermalPrinterComponent *parent) { this->parent_ = parent; }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  ThermalPrinterComponent *parent_;
  uint32_t last_check_{0};
};

}  // namespace thermal_printer
}  // namespace esphome
