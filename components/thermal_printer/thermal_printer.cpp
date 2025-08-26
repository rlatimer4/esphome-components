#include "thermal_printer.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer";

// ASCII codes used
#define ASCII_TAB '\t'
#define ASCII_LF '\n'
#define ASCII_FF '\f'
#define ASCII_CR '\r'
#define ASCII_DC2 18
#define ASCII_ESC 27
#define ASCII_FS 28
#define ASCII_GS 29

// Global preferences object for paper usage persistence
static const uint32_t PAPER_USAGE_HASH = 0x12345678;

void ThermalPrinterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Thermal Printer with DTR...");
  
  // Initialize DTR pin if configured
  if (this->dtr_pin_ != nullptr) {
    this->dtr_pin_->setup();
    this->dtr_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    ESP_LOGI(TAG, "DTR pin configured on GPIO%d", this->dtr_pin_->get_pin());
  }
  
  // Initialize DTR timing calculations
  this->initialize_dtr_timings();
  
  delay(1000);
  
  // Clear any pending data
  while (this->available()) {
    this->read();
  }
  
  this->wake();
  this->set_heat_config(this->heat_dots_, this->heat_time_, this->heat_interval_);
  this->set_default();
  
  // Load usage data from flash
  this->load_usage_from_flash();
  
  // Initial paper check
  this->paper_status_ = this->has_paper();
  
  ESP_LOGCONFIG(TAG, "Printer setup complete - DTR: %s", 
                this->is_dtr_enabled() ? "ENABLED" : "Disabled");
}

void ThermalPrinterComponent::loop() {
  // Check paper status periodically
  if (millis() - this->last_paper_check_ > 10000) {
    this->last_paper_check_ = millis();
    bool current_status = this->has_paper();
    if (current_status != this->paper_status_) {
      this->paper_status_ = current_status;
      if (this->paper_check_callback_) {
        this->paper_check_callback_.value()(current_status);
      }
    }
  }
}

void ThermalPrinterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Thermal Printer:");
  ESP_LOGCONFIG(TAG, "  DTR: %s", this->is_dtr_enabled() ? "ENABLED" : "Disabled");
  if (this->is_dtr_enabled()) {
    ESP_LOGCONFIG(TAG, "  DTR Pin: GPIO%d", this->dtr_pin_->get_pin());
    ESP_LOGCONFIG(TAG, "  DTR Timeouts: %u", this->dtr_timeout_count_);
  }
}

// ===== DTR METHODS =====

void ThermalPrinterComponent::initialize_dtr_timings() {
  uint32_t baud_rate = this->parent_ ? this->parent_->get_baud_rate() : 19200;
  this->byte_time_micros_ = (10 * 1000000) / baud_rate;
  this->dot_print_time_micros_ = 33;
  this->dot_feed_time_micros_ = 333;
}

void ThermalPrinterComponent::timeout_wait() {
  if (this->is_dtr_enabled()) {
    uint32_t start_time = millis();
    uint32_t timeout_ms = 5000;
    
    while (!this->dtr_ready()) {
      if (millis() - start_time > timeout_ms) {
        this->dtr_timeout_count_++;
        ESP_LOGW(TAG, "DTR timeout after %ums", timeout_ms);
        break;
      }
      delay(1);
    }
    this->dtr_waits_++;
  } else {
    while (micros() < this->resume_time_micros_) {
      delayMicroseconds(10);
    }
    this->timeout_waits_++;
  }
}

void ThermalPrinterComponent::timeout_set(uint32_t delay_microseconds) {
  this->resume_time_micros_ = micros() + delay_microseconds;
}

bool ThermalPrinterComponent::dtr_ready() {
  if (!this->is_dtr_enabled()) {
    return true;
  }
  return !this->dtr_pin_->digital_read();
}

void ThermalPrinterComponent::wait_for_printer_ready(uint32_t timeout_ms) {
  if (!this->is_dtr_enabled()) {
    delay(timeout_ms / 10);
    return;
  }
  
  uint32_t start_time = millis();
  while (!this->dtr_ready()) {
    if (millis() - start_time > timeout_ms) {
      this->dtr_timeout_count_++;
      break;
    }
    delay(1);
  }
}

void ThermalPrinterComponent::write_byte_with_flow_control(uint8_t byte) {
  this->timeout_wait();
  this->write_byte(byte);
  this->total_bytes_sent_++;
  
  if (this->is_dtr_enabled()) {
    this->timeout_set(this->byte_time_micros_ / 4);
  } else {
    this->timeout_set(this->byte_time_micros_ * 2);
  }
}

// ===== PRINT INTERFACE =====

size_t ThermalPrinterComponent::write(uint8_t c) {
  this->write_byte_with_flow_control(c);
  
  this->characters_printed_++;
  if (c == '\n') {
    this->lines_printed_++;
    if (this->is_dtr_enabled()) {
      this->timeout_set(this->dot_feed_time_micros_ * 8);
    } else {
      this->timeout_set(this->dot_feed_time_micros_ * 16);
    }
  }
  
  if (this->characters_printed_ % 100 == 0) {
    this->save_usage_to_flash();
  }
  
  return 1;
}

size_t ThermalPrinterComponent::write(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {
    this->write(buffer[i]);
  }
  return size;
}

// ===== PRINTER CONTROL METHODS =====

void ThermalPrinterComponent::wake() {
  this->write_byte_with_flow_control(255);
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(3000);
  } else {
    delay(50);
  }
  this->set_heat_config(this->heat_dots_, this->heat_time_, this->heat_interval_);
}

void ThermalPrinterComponent::sleep() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('8');
  this->write_byte_with_flow_control(0);
  this->write_byte_with_flow_control(0);
}

void ThermalPrinterComponent::reset() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('@');
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(5000);
  } else {
    delay(500);
  }
}

void ThermalPrinterComponent::set_default() {
  this->online();
  this->justify('L');
  this->inverse_off();
  this->bold_off();
  this->underline_off();
  this->set_size('S');
  this->set_line_height();
}

void ThermalPrinterComponent::test() {
  this->print_text("Hello World!");
  this->feed(2);
}

void ThermalPrinterComponent::set_heat_config(uint8_t dots, uint8_t time, uint8_t interval) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('7');
  this->write_byte_with_flow_control(dots);
  this->write_byte_with_flow_control(time);
  this->write_byte_with_flow_control(interval);
  
  this->heat_dots_ = dots;
  this->heat_time_ = time;
  this->heat_interval_ = interval;
}

void ThermalPrinterComponent::bold_on(bool state) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('E');
  this->write_byte_with_flow_control(state ? 1 : 0);
}

void ThermalPrinterComponent::bold_off() {
  this->bold_on(false);
}

void ThermalPrinterComponent::underline_on(bool state) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('-');
  this->write_byte_with_flow_control(state ? 1 : 0);
}

void ThermalPrinterComponent::underline_off() {
  this->underline_on(false);
}

void ThermalPrinterComponent::inverse_on(bool state) {
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('B');
  this->write_byte_with_flow_control(state ? 1 : 0);
}

void ThermalPrinterComponent::inverse_off() {
  this->inverse_on(false);
}

void ThermalPrinterComponent::set_size(char value) {
  uint8_t size = 0;
  switch (value) {
    case 'L': size = 0x30; break;
    case 'M': size = 0x10; break;
    case 'S':
    default: size = 0x00; break;
  }
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(size);
}

void ThermalPrinterComponent::set_text_size(uint8_t size) {
  char size_char = 'S';
  if (size == 1) size_char = 'S';
  else if (size == 2) size_char = 'M';
  else if (size >= 3) size_char = 'L';
  this->set_size(size_char);
}

void ThermalPrinterComponent::set_line_height(uint8_t height) {
  if (height < 24) height = 24;
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('3');
  this->write_byte_with_flow_control(height);
}

void ThermalPrinterComponent::justify(char value) {
  uint8_t pos = 0;
  switch (value) {
    case 'L': pos = 0; break;
    case 'C': pos = 1; break;
    case 'R': pos = 2; break;
  }
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('a');
  this->write_byte_with_flow_control(pos);
}

void ThermalPrinterComponent::feed(uint8_t x) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('d');
  this->write_byte_with_flow_control(x);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(x * 100 + 1000);
  } else {
    delay(x * 50 + 200);
  }
  
  this->track_print_operation(0, 0, x);
}

void ThermalPrinterComponent::print_barcode(int type, const char *text) {
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('H');
  this->write_byte_with_flow_control(2);
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('w');
  this->write_byte_with_flow_control(3);
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control((uint8_t)type);
  
  for (const char *p = text; *p != 0; p++) {
    this->write_byte_with_flow_control(*p);
  }
  this->write_byte_with_flow_control(0);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(5000);
  } else {
    delay(300);
  }
  
  this->track_print_operation(strlen(text), 3, 0);
}

void ThermalPrinterComponent::online() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('=');
  this->write_byte_with_flow_control(1);
}

// ===== TEXT PRINTING =====

void ThermalPrinterComponent::print_text(const char *text) {
  if (!text || strlen(text) == 0) return;
  
  this->print(text);
  
  uint8_t newlines = 0;
  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    if (text[i] == '\n') newlines++;
  }
  
  this->track_print_operation(len, newlines, 0);
}

void ThermalPrinterComponent::set_rotation(uint8_t rotation) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('V');
  this->write_byte_with_flow_control(rotation & 0x03);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(500);
  } else {
    delay(50);
  }
}

void ThermalPrinterComponent::print_rotated_text(const char* text, uint8_t rotation) {
  if (!text || strlen(text) == 0) return;
  
  // Force small size for rotation stability
  this->set_text_size(1);
  this->justify('C');
  this->set_rotation(1);
  
  size_t text_len = strlen(text);
  size_t max_chars = text_len > 20 ? 20 : text_len;
  
  for (size_t i = 0; i < max_chars; i++) {
    if (text[i] == ' ') {
      this->print_text("·");
      this->feed(1);
    } else if (text[i] != '\n') {
      char single_char[2] = {text[i], '\0'};
      this->print_text(single_char);
      this->feed(2);
      
      if (this->is_dtr_enabled()) {
        this->wait_for_printer_ready(2000);
      } else {
        delay(100);
      }
    }
  }
  
  this->set_rotation(0);
  this->justify('L');
  this->set_text_size(2);
  this->feed(3);
}

void ThermalPrinterComponent::print_two_column(const char *left_text, const char *right_text, bool fill_dots, char text_size) {
  this->set_size(text_size);
  
  uint8_t line_width = 32;
  if (text_size == 'M') line_width = 24;
  else if (text_size == 'L') line_width = 16;
  
  char pad_char = fill_dots ? '.' : ' ';
  this->print_padded_line(left_text, right_text, line_width, pad_char);
  this->set_size('S');
}

bool ThermalPrinterComponent::has_paper() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('v');
  this->write_byte_with_flow_control(0);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(1000);
  } else {
    delay(100);
  }
  
  if (this->available()) {
    uint8_t status = this->read();
    return (status & 0x0C) == 0;
  }
  return true;
}

// ===== PAPER USAGE METHODS =====

float ThermalPrinterComponent::get_paper_usage_mm() {
  return (this->lines_printed_ + this->feeds_executed_) * this->line_height_mm_;
}

float ThermalPrinterComponent::get_paper_usage_percent() {
  return (this->get_paper_usage_mm() / this->paper_roll_length_) * 100.0;
}

void ThermalPrinterComponent::reset_paper_usage() {
  this->lines_printed_ = 0;
  this->characters_printed_ = 0;
  this->feeds_executed_ = 0;
  this->save_usage_to_flash();
}

uint32_t ThermalPrinterComponent::get_lines_printed() {
  return this->lines_printed_;
}

uint32_t ThermalPrinterComponent::get_characters_printed() {
  return this->characters_printed_;
}

void ThermalPrinterComponent::set_paper_roll_length(float length_mm) {
  this->paper_roll_length_ = length_mm;
}

void ThermalPrinterComponent::set_line_height_calibration(float mm_per_line) {
  this->line_height_mm_ = mm_per_line;
}

void ThermalPrinterComponent::set_paper_check_callback(std::function<void(bool)> &&callback) {
  this->paper_check_callback_ = callback;
}

// ===== HELPER METHODS =====

void ThermalPrinterComponent::track_print_operation(uint16_t chars, uint8_t lines, uint8_t feeds) {
  this->characters_printed_ += chars;
  this->lines_printed_ += lines;
  this->feeds_executed_ += feeds;
}

void ThermalPrinterComponent::save_usage_to_flash() {
  struct UsageData {
    uint32_t lines_printed;
    uint32_t characters_printed;
    uint32_t feeds_executed;
  } data;
  
  data.lines_printed = this->lines_printed_;
  data.characters_printed = this->characters_printed_;
  data.feeds_executed = this->feeds_executed_;
  
  auto pref = global_preferences->make_preference<UsageData>(PAPER_USAGE_HASH);
  pref.save(&data);
}

void ThermalPrinterComponent::load_usage_from_flash() {
  struct UsageData {
    uint32_t lines_printed;
    uint32_t characters_printed;
    uint32_t feeds_executed;
  } data;
  
  auto pref = global_preferences->make_preference<UsageData>(PAPER_USAGE_HASH);
  if (pref.load(&data)) {
    this->lines_printed_ = data.lines_printed;
    this->characters_printed_ = data.characters_printed;
    this->feeds_executed_ = data.feeds_executed;
  }
}

void ThermalPrinterComponent::print_padded_line(const char *left, const char *right, uint8_t total_width, char pad_char) {
  uint8_t left_len = strlen(left);
  uint8_t right_len = strlen(right);
  uint8_t padding = total_width - left_len - right_len;
  if (padding < 1) padding = 1;
  
  this->print(left);
  for (uint8_t i = 0; i < padding; i++) {
    this->write(pad_char);
  }
  this->print(right);
  this->print("\n");
  
  this->track_print_operation(total_width + 1, 1, 0);
}

void ThermalPrinterComponent::update_performance_stats() {
  // Performance tracking implementation
}

bool ThermalPrinterComponent::validate_config() {
  return this->parent_ != nullptr;
}

}  // namespace thermal_printer
}  // namespace esphome
