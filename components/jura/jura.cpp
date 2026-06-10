#include "jura.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdlib>

namespace esphome {
namespace jura {

static const char *const TAG = "jura";

// Milliseconds between encoded data bytes on the wire
static const uint32_t INTER_BYTE_DELAY_MS = 8;

void JuraCoffeeComponent::setup() {
  if (this->single_espresso_sensor_ == nullptr && this->double_espresso_sensor_ == nullptr &&
      this->coffee_sensor_ == nullptr && this->double_coffee_sensor_ == nullptr &&
      this->cleanings_sensor_ == nullptr) {
    ESP_LOGW(TAG, "No sensors configured - component will poll but not publish data");
  }
}

void JuraCoffeeComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Jura Coffee Machine:");
  ESP_LOGCONFIG(TAG, "  Timeout: %" PRIu32 " ms", this->timeout_ms_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Single Espresso", this->single_espresso_sensor_);
  LOG_SENSOR("  ", "Double Espresso", this->double_espresso_sensor_);
  LOG_SENSOR("  ", "Coffee", this->coffee_sensor_);
  LOG_SENSOR("  ", "Double Coffee", this->double_coffee_sensor_);
  LOG_SENSOR("  ", "Cleanings", this->cleanings_sensor_);
}

void JuraCoffeeComponent::update() {
  if (this->state_ != State::IDLE) {
    ESP_LOGW(TAG, "Previous transaction still in progress, skipping update");
    return;
  }
  ESP_LOGD(TAG, "Polling Jura Coffee Machine for data...");
  this->start_command_("RT:0000");
}

void JuraCoffeeComponent::start_command_(const char *command) {
  // Discard any stale data before starting a new transaction
  uint8_t discard;
  while (this->available()) {
    this->read_byte(&discard);
  }

  this->tx_buffer_ = command;
  this->tx_buffer_ += "\r\n";
  this->tx_index_ = 0;
  this->last_tx_time_ = millis() - INTER_BYTE_DELAY_MS;
  this->state_ = State::SENDING;
}

void JuraCoffeeComponent::loop() {
  const uint32_t now = millis();
  switch (this->state_) {
    case State::IDLE:
      break;

    case State::SENDING: {
      if (now - this->last_tx_time_ < INTER_BYTE_DELAY_MS)
        break;
      // Encode one data byte as four UART bytes, two bits per byte
      const uint8_t c = this->tx_buffer_[this->tx_index_];
      for (uint8_t bit = 0; bit < 8; bit += 2) {
        uint8_t raw = 0xFF;
        if (!((c >> bit) & 0x01))
          raw &= ~(1 << 2);
        if (!((c >> (bit + 1)) & 0x01))
          raw &= ~(1 << 5);
        this->write_byte(raw);
      }
      this->last_tx_time_ = now;
      if (++this->tx_index_ >= this->tx_buffer_.size()) {
        this->rx_buffer_.clear();
        this->rx_byte_ = 0;
        this->rx_bit_pos_ = 0;
        this->rx_start_time_ = now;
        this->state_ = State::RECEIVING;
      }
      break;
    }

    case State::RECEIVING: {
      while (this->available()) {
        uint8_t raw;
        if (!this->read_byte(&raw))
          break;
        if ((raw >> 2) & 0x01)
          this->rx_byte_ |= 1 << this->rx_bit_pos_;
        if ((raw >> 5) & 0x01)
          this->rx_byte_ |= 1 << (this->rx_bit_pos_ + 1);
        this->rx_bit_pos_ += 2;
        if (this->rx_bit_pos_ >= 8) {
          this->rx_buffer_ += static_cast<char>(this->rx_byte_);
          this->rx_byte_ = 0;
          this->rx_bit_pos_ = 0;
          if (this->rx_buffer_.size() >= 2 && this->rx_buffer_.compare(this->rx_buffer_.size() - 2, 2, "\r\n") == 0) {
            this->state_ = State::IDLE;
            this->handle_response_(this->rx_buffer_.substr(0, this->rx_buffer_.size() - 2));
            return;
          }
        }
      }
      if (now - this->rx_start_time_ > this->timeout_ms_) {
        ESP_LOGW(TAG, "Timeout waiting for response from coffee machine - check connection");
        this->state_ = State::IDLE;
      }
      break;
    }
  }
}

void JuraCoffeeComponent::handle_response_(const std::string &response) {
  ESP_LOGD(TAG, "Received counter data: %s", response.c_str());

  if (response.length() < 39) {
    ESP_LOGW(TAG, "Counter response too short: %zu chars (expected >= 39)", response.length());
    return;
  }

  const long num_single_espresso = strtol(response.substr(3, 4).c_str(), nullptr, 16);
  const long num_double_espresso = strtol(response.substr(7, 4).c_str(), nullptr, 16);
  const long num_coffee = strtol(response.substr(11, 4).c_str(), nullptr, 16);
  const long num_double_coffee = strtol(response.substr(15, 4).c_str(), nullptr, 16);
  const long num_clean = strtol(response.substr(35, 4).c_str(), nullptr, 16);

  if (this->single_espresso_sensor_ != nullptr)
    this->single_espresso_sensor_->publish_state(num_single_espresso);
  if (this->double_espresso_sensor_ != nullptr)
    this->double_espresso_sensor_->publish_state(num_double_espresso);
  if (this->coffee_sensor_ != nullptr)
    this->coffee_sensor_->publish_state(num_coffee);
  if (this->double_coffee_sensor_ != nullptr)
    this->double_coffee_sensor_->publish_state(num_double_coffee);
  if (this->cleanings_sensor_ != nullptr)
    this->cleanings_sensor_->publish_state(num_clean);

  ESP_LOGD(TAG, "Coffee counters - Single: %ld, Double: %ld, Coffee: %ld, Double Coffee: %ld, Clean: %ld",
           num_single_espresso, num_double_espresso, num_coffee, num_double_coffee, num_clean);
}

}  // namespace jura
}  // namespace esphome
