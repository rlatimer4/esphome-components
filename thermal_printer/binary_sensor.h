#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "thermal_printer.h"

namespace esphome {
namespace thermal_printer {

class ThermalPrinterBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_parent(ThermalPrinterComponent *parent) { this->parent_ = parent; }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  ThermalPrinterComponent *parent_;
  uint32_t last_check_{0};
  bool last_state_{true};
};

}  // namespace thermal_printer
}  // namespace esphome
