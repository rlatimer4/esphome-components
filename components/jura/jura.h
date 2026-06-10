#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

#include <string>

namespace esphome {
namespace jura {

class JuraCoffeeComponent : public PollingComponent, public uart::UARTDevice {
 public:
  void set_single_espresso_sensor(sensor::Sensor *s) { this->single_espresso_sensor_ = s; }
  void set_double_espresso_sensor(sensor::Sensor *s) { this->double_espresso_sensor_ = s; }
  void set_coffee_sensor(sensor::Sensor *s) { this->coffee_sensor_ = s; }
  void set_double_coffee_sensor(sensor::Sensor *s) { this->double_coffee_sensor_ = s; }
  void set_cleanings_sensor(sensor::Sensor *s) { this->cleanings_sensor_ = s; }

  void set_timeout_ms(uint32_t timeout) { this->timeout_ms_ = timeout; }

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // The Jura protocol obfuscates each data byte across four UART bytes,
  // two bits at a time (in bits 2 and 5), with an ~8 ms pause between
  // data bytes. Communication runs as a non-blocking state machine in
  // loop() so the main loop is never stalled.
  enum class State : uint8_t { IDLE, SENDING, RECEIVING };

  void start_command_(const char *command);
  void handle_response_(const std::string &response);

  uint32_t timeout_ms_{5000};

  State state_{State::IDLE};
  std::string tx_buffer_;
  size_t tx_index_{0};
  uint32_t last_tx_time_{0};
  std::string rx_buffer_;
  uint8_t rx_byte_{0};
  uint8_t rx_bit_pos_{0};
  uint32_t rx_start_time_{0};

  sensor::Sensor *single_espresso_sensor_{nullptr};
  sensor::Sensor *double_espresso_sensor_{nullptr};
  sensor::Sensor *coffee_sensor_{nullptr};
  sensor::Sensor *double_coffee_sensor_{nullptr};
  sensor::Sensor *cleanings_sensor_{nullptr};
};

}  // namespace jura
}  // namespace esphome
