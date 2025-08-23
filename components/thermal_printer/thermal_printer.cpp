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
  ESP_LOGCONFIG(TAG, "Setting up Thermal Printer...");
  
  // Wait for printer to initialize
  delay(500);
  
  // Wake up and initialize printer
  this->wake();
  
  // Set heat config
  this->set_heat_config(7, 80, 2);
  
  // Set defaults
  this->set_default();
  
  // Load paper usage from flash
  this->load_usage_from_flash();
  
  ESP_LOGCONFIG(TAG, "Thermal Printer setup complete");
  ESP_LOGCONFIG(TAG, "Paper usage: %.1f mm (%.1f%% of roll)", 
                this->get_paper_usage_mm(), this->get_paper_usage_percent());
}

void ThermalPrinterComponent::loop() {
  // Check paper status every 10 seconds
  if (millis() - this->last_paper_check_ > 10000) {
    this->last_paper_check_ = millis();
    bool current_status = this->has_paper();
    if (current_status != this->paper_status_) {
      this->paper_status_ = current_status;
      if (this->paper_check_callback_) {
        this->paper_check_callback_.value()(current_status);
      }
      ESP_LOGD(TAG, "Paper status changed: %s", current_status ? "Present" : "Out");
    }
  }
}

void ThermalPrinterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Thermal Printer:");
  ESP_LOGCONFIG(TAG, "  Baud Rate: %d", this->parent_->get_baud_rate());
  ESP_LOGCONFIG(TAG, "  Lines Printed: %u", this->lines_printed_);
  ESP_LOGCONFIG(TAG, "  Characters Printed: %u", this->characters_printed_);
  ESP_LOGCONFIG(TAG, "  Paper Usage: %.1f mm (%.1f%%)", 
                this->get_paper_usage_mm(), this->get_paper_usage_percent());
}

// Print interface implementation
size_t ThermalPrinterComponent::write(uint8_t c) {
  this->write_byte(c);
  
  // Track character printing
  this->characters_printed_++;
  if (c == '\n') {
    this->lines_printed_++;
  }
  
  // Save usage periodically (every 100 characters to avoid excessive flash writes)
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

// Thermal printer control methods
void ThermalPrinterComponent::wake() {
  this->write_bytes(255);
  delay(50);
  this->set_heat_config(7, 80, 2);
}

void ThermalPrinterComponent::sleep() {
  this->write_bytes(ASCII_ESC, '8', 0, 0);
}

void ThermalPrinterComponent::reset() {
  this->write_bytes(ASCII_ESC, '@');
}

void ThermalPrinterComponent::set_default() {
  this->online();
  this->justify('L');
  this->inverse_off();
  this->double_height_off();
  this->set_line_height();
  this->bold_off();
  this->underline_off();
  this->set_bar_code_height();
  this->set_size('s');
  this->set_charset();
  this->set_code_page();
}

void ThermalPrinterComponent::test() {
  this->print_text("Hello World!");
  this->feed(2);
}

void ThermalPrinterComponent::test_page() {
  this->write_bytes(ASCII_DC2, 'T');
  this->track_print_operation(0, 10, 0); // Estimate test page usage
}

void ThermalPrinterComponent::set_heat_config(uint8_t dots, uint8_t time, uint8_t interval) {
  this->write_bytes(ASCII_ESC, '7');
  this->write_bytes(dots);
  this->write_bytes(time);
  this->write_bytes(interval);
  
  this->write_bytes(ASCII_ESC, 'D');
  this->write_bytes(4, 8, 12, 16);
  this->write_bytes(20, 24, 28, 0);
}

void ThermalPrinterComponent::normal() {
  this->write_bytes(ASCII_ESC, '!', 0);
}

void ThermalPrinterComponent::inverse_on(bool state) {
  this->write_bytes(ASCII_GS, 'B', state ? 1 : 0);
}

void ThermalPrinterComponent::inverse_off() {
  this->inverse_on(false);
}

void ThermalPrinterComponent::bold_on(bool state) {
  this->write_bytes(ASCII_ESC, 'E', state ? 1 : 0);
}

void ThermalPrinterComponent::bold_off() {
  this->bold_on(false);
}

void ThermalPrinterComponent::underline_on(bool state) {
  this->write_bytes(ASCII_ESC, '-', state ? 1 : 0);
}

void ThermalPrinterComponent::underline_off() {
  this->underline_on(false);
}

void ThermalPrinterComponent::double_height_on(bool state) {
  uint8_t value = state ? 0x10 : 0x00;
  this->write_bytes(ASCII_ESC, '!', value);
}

void ThermalPrinterComponent::double_height_off() {
  this->double_height_on(false);
}

void ThermalPrinterComponent::double_width_on(bool state) {
  uint8_t value = state ? 0x20 : 0x00;
  this->write_bytes(ASCII_ESC, '!', value);
}

void ThermalPrinterComponent::double_width_off() {
  this->double_width_on(false);
}

void ThermalPrinterComponent::set_size(char value) {
  uint8_t size = 0;
  switch (value) {
    case 'L':
      size = 0x30; // Double width and height
      break;
    case 'M':
      size = 0x10; // Double height
      break;
    case 'S':
    default:
      size = 0x00; // Normal
      break;
  }
  this->write_bytes(ASCII_ESC, '!', size);
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
  this->write_bytes(ASCII_ESC, '3', height);
}

void ThermalPrinterComponent::set_bar_code_height(uint8_t height) {
  if (height < 1) height = 1;
  this->write_bytes(ASCII_GS, 'h', height);
}

void ThermalPrinterComponent::set_charset(uint8_t charset) {
  this->write_bytes(ASCII_ESC, 'R', charset);
}

void ThermalPrinterComponent::set_code_page(uint8_t codePage) {
  this->write_bytes(ASCII_ESC, 't', codePage);
}

void ThermalPrinterComponent::justify(char value) {
  uint8_t pos = 0;
  switch (value) {
    case 'L': pos = 0; break;
    case 'C': pos = 1; break;
    case 'R': pos = 2; break;
  }
  this->write_bytes(ASCII_ESC, 'a', pos);
}

void ThermalPrinterComponent::feed(uint8_t x) {
  this->write_bytes(ASCII_ESC, 'd', x);
  this->track_print_operation(0, 0, x);
}

void ThermalPrinterComponent::feed_rows(uint8_t rows) {
  this->write_bytes(ASCII_ESC, 'J', rows);
  this->track_print_operation(0, 0, rows);
}

void ThermalPrinterComponent::print_barcode(const char *text, uint8_t type) {
  this->write_bytes(ASCII_GS, 'H', 2);
  this->write_bytes(ASCII_GS, 'w', 3);
  this->write_bytes(ASCII_GS, 'k', type);
  
  for (const char *p = text; *p != 0; p++) {
    this->write(*p);
  }
  
  this->write_bytes(0);
  delay(300);
  
  // Track barcode printing (estimate 3 lines)
  this->track_print_operation(strlen(text), 3, 0);
}

void ThermalPrinterComponent::print_barcode(int type, const char *text) {
  this->print_barcode(text, (uint8_t)type);
}

void ThermalPrinterComponent::online() {
  this->write_bytes(ASCII_ESC, '=', 1);
}

void ThermalPrinterComponent::offline() {
  this->write_bytes(ASCII_ESC, '=', 0);
}

// ESPHome specific methods
void ThermalPrinterComponent::print_text(const char *text) {
  size_t len = strlen(text);
  this->print(text);
  
  // Count newlines for line tracking
  uint8_t newlines = 0;
  for (size_t i = 0; i < len; i++) {
    if (text[i] == '\n') newlines++;
  }
  
  this->track_print_operation(len, newlines, 0);
}

void ThermalPrinterComponent::print_two_column(const char *left_text, const char *right_text, bool fill_dots) {
  const uint8_t line_width = 32; // Characters per line for normal text
  char pad_char = fill_dots ? '.' : ' ';
  
  // Split text into lines
  std::string left(left_text);
  std::string right(right_text);
  
  size_t left_pos = 0, right_pos = 0;
  
  while (left_pos < left.length() || right_pos < right.length()) {
    // Extract left column text (max 15 chars to leave room for right side)
    std::string left_line;
    size_t left_end = left_pos;
    uint8_t left_chars = 0;
    
    while (left_end < left.length() && left_chars < 15) {
      if (left[left_end] == '\n') {
        break;
      }
      left_line += left[left_end];
      left_end++;
      left_chars++;
    }
    
    if (left_end < left.length() && left[left_end] == '\n') {
      left_end++; // Skip newline
    }
    left_pos = left_end;
    
    // Extract right column text (max 15 chars)
    std::string right_line;
    size_t right_end = right_pos;
    uint8_t right_chars = 0;
    
    while (right_end < right.length() && right_chars < 15) {
      if (right[right_end] == '\n') {
        break;
      }
      right_line += right[right_end];
      right_end++;
      right_chars++;
    }
    
    if (right_end < right.length() && right[right_end] == '\n') {
      right_end++; // Skip newline
    }
    right_pos = right_end;
    
    // Print the padded line
    this->print_padded_line(left_line.c_str(), right_line.c_str(), line_width, pad_char);
  }
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
  this->write_bytes(ASCII_ESC, 'v', 0);
  delay(100);
  
  if (this->available()) {
    uint8_t status = this->read();
    return (status & 0x0C) == 0;
  }
  
  return true; // Default to paper present
}

// Paper usage estimation methods
float ThermalPrinterComponent::get_paper_usage_mm() {
  // Estimate based on lines printed and feeds
  float usage = (this->lines_printed_ + this->feeds_executed_) * this->line_height_mm_;
  return usage;
}

float ThermalPrinterComponent::get_paper_usage_percent() {
  float usage_mm = this->get_paper_usage_mm();
  return (usage_mm / this->paper_roll_length_) * 100.0;
}

void ThermalPrinterComponent::reset_paper_usage() {
  this->lines_printed_ = 0;
  this->characters_printed_ = 0;
  this->feeds_executed_ = 0;
  this->save_usage_to_flash();
  
  ESP_LOGI(TAG, "Paper usage counters reset");
}

uint32_t ThermalPrinterComponent::get_lines_printed() {
  return this->lines_printed_;
}

uint32_t ThermalPrinterComponent::get_characters_printed() {
  return this->characters_printed_;
}

void ThermalPrinterComponent::set_paper_roll_length(float length_mm) {
  this->paper_roll_length_ = length_mm;
  ESP_LOGI(TAG, "Paper roll length set to %.0f mm", length_mm);
}

void ThermalPrinterComponent::set_paper_check_callback(std::function<void(bool)> &&callback) {
  this->paper_check_callback_ = callback;
}

// Helper methods
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
    
    ESP_LOGI(TAG, "Loaded paper usage from flash: %u lines, %u chars, %u feeds", 
             this->lines_printed_, this->characters_printed_, this->feeds_executed_);
  }
}

void ThermalPrinterComponent::print_padded_line(const char *left, const char *right, uint8_t total_width, char pad_char) {
  uint8_t left_len = strlen(left);
  uint8_t right_len = strlen(right);
  
  // Calculate padding needed
  uint8_t padding = total_width - left_len - right_len;
  if (padding < 1) padding = 1; // At least one space/dot
  
  // Print left text
  this->print(left);
  
  // Print padding
  for (uint8_t i = 0; i < padding; i++) {
    this->write(pad_char);
  }
  
  // Print right text
  this->print(right);
  this->print("\n");
  
  this->track_print_operation(total_width + 1, 1, 0);
}

void ThermalPrinterComponent::write_bytes(uint8_t a) {
  this->write_byte(a);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b) {
  this->write_byte(a);
  this->write_byte(b);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b, uint8_t c) {
  this->write_byte(a);
  this->write_byte(b);
  this->write_byte(c);
}

void ThermalPrinterComponent::write_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  this->write_byte(a);
  this->write_byte(b);
  this->write_byte(c);
  this->write_byte(d);
}

}  // namespace thermal_printer
}  // namespace esphome
