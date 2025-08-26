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

void ThermalPrinterComponent::write_bytes_with_flow_control(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    this->write_byte_with_flow_control(data[i]);
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

void ThermalPrinterComponent::test() {
  this->print_text("Hello World!");
  this->feed(2);
}

void ThermalPrinterComponent::test_page() {
  this->write_byte_with_flow_control(ASCII_DC2);
  this->write_byte_with_flow_control('T');
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(10000);
  } else {
    delay(1000);
  }
  
  this->track_print_operation(0, 10, 0);
}

void ThermalPrinterComponent::normal() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(0);
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

void ThermalPrinterComponent::double_height_on(bool state) {
  uint8_t value = state ? 0x10 : 0x00;
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(value);
}

void ThermalPrinterComponent::double_height_off() {
  this->double_height_on(false);
}

void ThermalPrinterComponent::double_width_on(bool state) {
  uint8_t value = state ? 0x20 : 0x00;
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(value);
}

void ThermalPrinterComponent::double_width_off() {
  this->double_width_on(false);
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

void ThermalPrinterComponent::set_bar_code_height(uint8_t height) {
  if (height < 1) height = 1;
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('h');
  this->write_byte_with_flow_control(height);
}

void ThermalPrinterComponent::set_charset(uint8_t charset) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('R');
  this->write_byte_with_flow_control(charset);
}

void ThermalPrinterComponent::set_code_page(uint8_t codePage) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('t');
  this->write_byte_with_flow_control(codePage);
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

void ThermalPrinterComponent::feed_rows(uint8_t rows) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('J');
  this->write_byte_with_flow_control(rows);
  this->track_print_operation(0, 0, rows);
}

void ThermalPrinterComponent::print_barcode(const char *text, uint8_t type) {
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('H');
  this->write_byte_with_flow_control(2);
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('w');
  this->write_byte_with_flow_control(3);
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control(type);
  
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

void ThermalPrinterComponent::print_barcode(int type, const char *text) {
  this->print_barcode(text, (uint8_t)type);
}

void ThermalPrinterComponent::online() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('=');
  this->write_byte_with_flow_control(1);
}

void ThermalPrinterComponent::offline() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('=');
  this->write_byte_with_flow_control(0);
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

void ThermalPrinterComponent::print_qr_code(const char* data, uint8_t size, uint8_t error_correction) {
  if (!data || strlen(data) == 0) {
    ESP_LOGW(TAG, "Empty QR code data");
    return;
  }
  
  if (strlen(data) > 2048) {
    ESP_LOGW(TAG, "QR code data too long");
    return;
  }
  
  // QR Code Model 2 commands
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('(');
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control(4);
  this->write_byte_with_flow_control(0);
  this->write_byte_with_flow_control(49);
  this->write_byte_with_flow_control(65);
  this->write_byte_with_flow_control(size);
  this->write_byte_with_flow_control(0);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(1000);
  } else {
    delay(10);
  }
  
  // Set error correction
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('(');
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control(3);
  this->write_byte_with_flow_control(0);
  this->write_byte_with_flow_control(49);
  this->write_byte_with_flow_control(67);
  this->write_byte_with_flow_control(error_correction);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(1000);
  } else {
    delay(10);
  }
  
  // Store QR data
  uint16_t data_len = strlen(data);
  uint16_t total_len = data_len + 3;
  
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('(');
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control(total_len & 0xFF);
  this->write_byte_with_flow_control((total_len >> 8) & 0xFF);
  this->write_byte_with_flow_control(49);
  this->write_byte_with_flow_control(80);
  this->write_byte_with_flow_control(48);
  
  // Send data
  for (size_t i = 0; i < data_len; i++) {
    this->write_byte_with_flow_control(data[i]);
  }
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(3000);
  } else {
    delay(50);
  }
  
  // Print QR code
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('(');
  this->write_byte_with_flow_control('k');
  this->write_byte_with_flow_control(3);
  this->write_byte_with_flow_control(0);
  this->write_byte_with_flow_control(49);
  this->write_byte_with_flow_control(81);
  this->write_byte_with_flow_control(48);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(10000);
  } else {
    delay(300);
  }
  
  this->feed(2);
  this->track_print_operation(data_len, 8, 2);
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

void ThermalPrinterComponent::print_table_row(const char *col1, const char *col2, const char *col3) {
  if (col3 == nullptr) {
    // Two column table
    this->print_padded_line(col1, col2, 32, ' ');
  } else {
    // Three column table (10 chars each + 2 spaces)
    char line[33];
    snprintf(line, sizeof(line), "%-10.10s %-10.10s %-10.10s", col1, col2, col3);
    this->print(line);
    this->print("\n");
    
    this->track_print_operation(strlen(line) + 1, 1, 0);
  }
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

// ===== ENHANCED METHODS =====

PrintResult ThermalPrinterComponent::safe_print_text(const char* text) {
  if (!text || strlen(text) == 0) {
    return PrintResult::SUCCESS;
  }
  
  if (!this->has_paper()) {
    ESP_LOGW(TAG, "Cannot print: Paper out");
    return PrintResult::PAPER_OUT;
  }
  
  uint16_t estimated_lines = this->estimate_lines_for_text(text);
  
  if (!this->can_print_job(estimated_lines)) {
    ESP_LOGW(TAG, "Cannot print: Insufficient paper");
    return PrintResult::INSUFFICIENT_PAPER;
  }
  
  this->print_text(text);
  
  ESP_LOGI(TAG, "Print completed successfully");
  return PrintResult::SUCCESS;
}

bool ThermalPrinterComponent::get_detailed_status(PrinterStatus* status) {
  if (!status) return false;
  
  status->paper_present = this->has_paper();
  status->cover_open = false;
  status->cutter_error = false;
  status->printer_online = true;
  status->dtr_ready = this->dtr_ready();
  status->temperature_estimate = 25.0;
  status->last_response_time = millis();
  status->dtr_timeouts = this->dtr_timeout_count_;
  
  return true;
}

bool ThermalPrinterComponent::can_print_job(uint16_t estimated_lines) {
  float required_mm = estimated_lines * this->line_height_mm_;
  float remaining_mm = this->paper_roll_length_ - this->get_paper_usage_mm();
  
  return required_mm <= remaining_mm;
}

uint16_t ThermalPrinterComponent::estimate_lines_for_text(const char* text) {
  if (!text) return 0;
  
  uint16_t lines = 1;
  size_t text_len = strlen(text);
  
  for (size_t i = 0; i < text_len; i++) {
    if (text[i] == '\n') lines++;
  }
  
  uint16_t current_line_length = 0;
  for (size_t i = 0; i < text_len; i++) {
    if (text[i] == '\n') {
      current_line_length = 0;
    } else {
      current_line_length++;
      if (current_line_length >= 32) {
        lines++;
        current_line_length = 0;
      }
    }
  }
  
  return lines;
}

float ThermalPrinterComponent::predict_paper_usage_for_job(const char* text, uint8_t text_size) {
  if (!text) return 0.0;
  
  uint16_t lines = this->estimate_lines_for_text(text);
  
  float size_multiplier = 1.0;
  switch (text_size) {
    case 'L': size_multiplier = 2.0; break;
    case 'M': size_multiplier = 1.5; break;
    case 'S': 
    default:  size_multiplier = 1.0; break;
  }
  
  return lines * this->line_height_mm_ * size_multiplier;
}

void ThermalPrinterComponent::set_heat_config_advanced(uint8_t dots, uint8_t time, uint8_t interval, uint8_t density) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('7');
  this->write_byte_with_flow_control(dots & 0x0F);
  this->write_byte_with_flow_control(time & 0xFF);
  this->write_byte_with_flow_control(interval & 0xFF);
  
  this->write_byte_with_flow_control(ASCII_DC2);
  this->write_byte_with_flow_control('#');
  this->write_byte_with_flow_control((density << 4) | density);
  
  ESP_LOGD(TAG, "Set advanced heat config: dots=%d, time=%d, interval=%d, density=%d", 
           dots, time, interval, density);
}

void ThermalPrinterComponent::print_simple_receipt(const char* business_name, const char* total) {
  ESP_LOGI(TAG, "Printing simple receipt");
  
  if (!this->has_paper()) {
    ESP_LOGW(TAG, "Cannot print receipt: No paper");
    return;
  }
  
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  this->print_text(business_name ? business_name : "Receipt");
  this->bold_off();
  this->feed(2);
  
  this->justify('L');
  this->set_text_size(1);
  this->print_text("Date: [Current]");
  this->print_text("--------------------------------");
  this->feed(1);
  
  if (total) {
    this->set_text_size(1);
    this->bold_on();
    this->print_two_column("TOTAL:", total, true, 'S');
    this->bold_off();
  }
  
  this->feed(3);
  
  this->justify('C');
  this->print_text("Thank you!");
  this->feed(4);
  
  this->justify('L');
  this->set_text_size(2);
}

void ThermalPrinterComponent::print_shopping_list(const char* items_string) {
  if (!items_string || strlen(items_string) == 0) {
    ESP_LOGW(TAG, "Empty shopping list");
    return;
  }
  
  ESP_LOGI(TAG, "Printing shopping list");
  
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  this->print_text("SHOPPING LIST");
  this->bold_off();
  this->feed(2);
  
  this->set_text_size(1);
  this->print_text("Date: [Today]");
  this->feed(1);
  
  this->print_text("================================");
  this->feed(1);
  
  this->justify('L');
  char line[64];
  snprintf(line, sizeof(line), "1. [ ] %s", items_string);
  this->print_text(line);
  this->feed(1);
  
  this->feed(2);
  this->print_text("================================");
  this->feed(4);
  
  this->justify('L');
  this->set_text_size(2);
}

bool ThermalPrinterComponent::validate_config() {
  bool valid = true;
  
  if (!this->parent_) {
    ESP_LOGE(TAG, "UART parent not configured");
    valid = false;
  } else {
    uint32_t baud_rate = this->parent_->get_baud_rate();
    if (baud_rate != 9600 && baud_rate != 19200 && baud_rate != 38400) {
      ESP_LOGW(TAG, "Unusual baud rate: %d (recommended: 9600)", baud_rate);
    }
  }
  
  if (this->paper_roll_length_ <= 0) {
    ESP_LOGW(TAG, "Invalid paper roll length: %.1fmm", this->paper_roll_length_);
    this->paper_roll_length_ = 30000.0;
  }
  
  if (this->line_height_mm_ <= 0 || this->line_height_mm_ > 10.0) {
    ESP_LOGW(TAG, "Invalid line height: %.2fmm (setting to 4.0mm)", this->line_height_mm_);
    this->line_height_mm_ = 4.0;
  }
  
  ESP_LOGD(TAG, "Configuration validation %s", valid ? "passed" : "failed");
  return valid;
}

void ThermalPrinterComponent::print_startup_message() {
  ESP_LOGI(TAG, "Printing startup message");
  
  this->justify('C');
  this->set_text_size(2);
  this->bold_on();
  this->print_text("ESPHome Printer");
  this->bold_off();
  this->feed(1);
  
  this->set_text_size(1);
  this->print_text("Ready for printing!");
  this->feed(1);
  
  this->print_text("System Started");
  this->feed(1);
  
  this->justify('L');
  this->set_text_size(2);
  this->feed(2);
}

void ThermalPrinterComponent::recover_from_error() {
  ESP_LOGI(TAG, "Attempting error recovery");
  
  while (this->available()) {
    this->read();
  }
  
  this->reset();
  delay(1000);
  
  this->wake();
  delay(500);
  
  this->set_heat_config_advanced(7, 80, 2, 4);
  this->set_default();
  
  ESP_LOGI(TAG, "Error recovery completed");
}

void ThermalPrinterComponent::log_performance_stats() {
  uint32_t uptime_minutes = millis() / 60000;
  float chars_per_minute = uptime_minutes > 0 ? (float)this->characters_printed_ / uptime_minutes : 0;
  float lines_per_minute = uptime_minutes > 0 ? (float)this->lines_printed_ / uptime_minutes : 0;
  
  ESP_LOGI(TAG, "Performance stats:");
  ESP_LOGI(TAG, "  Uptime: %u minutes", uptime_minutes);
  ESP_LOGI(TAG, "  Characters/minute: %.1f", chars_per_minute);
  ESP_LOGI(TAG, "  Lines/minute: %.1f", lines_per_minute);
  ESP_LOGI(TAG, "  Paper efficiency: %.1f chars/mm", 
           this->get_paper_usage_mm() > 0 ? this->characters_printed_ / this->get_paper_usage_mm() : 0);
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

// ===== HELPER WRITE METHODS =====

void ThermalPrinterComponent::write_bytes(uint8_t a) {
  this->write_byte_with_flow_control(a);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b) {
  this->write_byte_with_flow_control(a);
  this->write_byte_with_flow_control(b);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b, uint8_t c) {
  this->write_byte_with_flow_control(a);
  this->write_byte_with_flow_control(b);
  this->write_byte_with_flow_control(c);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  this->write_byte_with_flow_control(a);
  this->write_byte_with_flow_control(b);
  this->write_byte_with_flow_control(c);
  this->write_byte_with_flow_control(d);
}

uint32_t ThermalPrinterComponent::calculate_operation_time_micros(uint8_t operation_type, uint8_t data_length) {
  // Calculate timing based on operation type
  switch (operation_type) {
    case 0: // Character printing
      return this->dot_print_time_micros_ * data_length;
    case 1: // Line feed
      return this->dot_feed_time_micros_ * data_length;
    case 2: // Barcode
      return this->dot_print_time_micros_ * data_length * 8;
    default:
      return this->byte_time_micros_ * data_length;
  }
}

}  // namespace thermal_printer
}  // namespace esphome
