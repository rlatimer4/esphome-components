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
  ESP_LOGCONFIG(TAG, "Setting up Enhanced Thermal Printer...");
  
  // Validate configuration first
  if (!this->validate_config()) {
    ESP_LOGE(TAG, "Configuration validation failed");
    this->mark_failed();
    return;
  }
  
  // Extended initialization wait for printer boot
  delay(1000);
  
  // Clear any pending data
  while (this->available()) {
    this->read();
  }
  
  // Wake up printer with multiple attempts
  bool printer_responded = false;
  for (int attempts = 0; attempts < 3 && !printer_responded; attempts++) {
    ESP_LOGD(TAG, "Wake attempt %d/3", attempts + 1);
    
    this->wake();
    delay(500);
    
    // Test communication with status query
    this->write_bytes(ASCII_ESC, 'v', 0);
    delay(100);
    
    if (this->available()) {
      this->read(); // Clear response
      printer_responded = true;
      ESP_LOGI(TAG, "Printer communication established");
    } else {
      ESP_LOGW(TAG, "No response from printer on attempt %d", attempts + 1);
    }
  }
  
  if (!printer_responded) {
    ESP_LOGE(TAG, "Failed to establish printer communication");
    // Don't fail completely - might work anyway
  }
  
  // Configure printer with conservative heat settings for reliability
  this->set_heat_config_advanced(7, 80, 2, 4);
  delay(100);
  
  // Set comprehensive defaults
  this->set_default();
  delay(100);
  
  // Load usage data from flash
  this->load_usage_from_flash();
  
  // Initial paper check
  bool has_paper = this->has_paper();
  this->paper_status_ = has_paper;
  
  ESP_LOGCONFIG(TAG, "Enhanced Thermal Printer setup complete");
  ESP_LOGCONFIG(TAG, "  Paper status: %s", has_paper ? "Present" : "Out");
  ESP_LOGCONFIG(TAG, "  Paper usage: %.1f mm (%.1f%% of %.1fm roll)", 
                this->get_paper_usage_mm(), 
                this->get_paper_usage_percent(),
                this->paper_roll_length_ / 1000.0);
  ESP_LOGCONFIG(TAG, "  Lines printed: %u", this->lines_printed_);
  ESP_LOGCONFIG(TAG, "  Characters printed: %u", this->characters_printed_);
  
  // Print startup message if paper is available
  if (has_paper && printer_responded) {
    this->print_startup_message();
  }
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
  ESP_LOGCONFIG(TAG, "Enhanced Thermal Printer:");
  ESP_LOGCONFIG(TAG, "  Baud Rate: %d", this->parent_->get_baud_rate());
  ESP_LOGCONFIG(TAG, "  Lines Printed: %u", this->lines_printed_);
  ESP_LOGCONFIG(TAG, "  Characters Printed: %u", this->characters_printed_);
  ESP_LOGCONFIG(TAG, "  Paper Usage: %.1f mm (%.1f%%)", 
                this->get_paper_usage_mm(), this->get_paper_usage_percent());
  ESP_LOGCONFIG(TAG, "  Features: Rotation=%s, QR Codes=%s", 
                "enabled", "enabled");
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

void ThermalPrinterComponent::print_two_column(const char *left_text, const char *right_text, bool fill_dots, char text_size) {
  // Set text size first
  this->set_size(text_size);
  
  // Determine line width based on text size
  uint8_t line_width;
  switch (text_size) {
    case 'L': line_width = 16; break;  // Large text: 16 chars per line
    case 'M': line_width = 24; break;  // Medium text: 24 chars per line  
    case 'S':
    default:  line_width = 32; break;  // Small text: 32 chars per line
  }
  
  char pad_char = fill_dots ? '.' : ' ';
  
  // Split text into lines
  std::string left(left_text);
  std::string right(right_text);
  
  size_t left_pos = 0, right_pos = 0;
  uint8_t max_left_chars = (line_width - 2) / 2;  // Leave space for right side
  uint8_t max_right_chars = line_width - max_left_chars - 1; // Remaining space
  
  while (left_pos < left.length() || right_pos < right.length()) {
    // Extract left column text
    std::string left_line;
    size_t left_end = left_pos;
    uint8_t left_chars = 0;
    
    while (left_end < left.length() && left_chars < max_left_chars) {
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
    
    // Extract right column text
    std::string right_line;
    size_t right_end = right_pos;
    uint8_t right_chars = 0;
    
    while (right_end < right.length() && right_chars < max_right_chars) {
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
  
  // Reset to normal size
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

void ThermalPrinterComponent::set_line_height_calibration(float mm_per_line) {
  this->line_height_mm_ = mm_per_line;
  ESP_LOGI(TAG, "Line height calibration set to %.2f mm/line", mm_per_line);
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

// ===== PHASE 1 ENHANCED METHODS =====

// 90-Degree Rotation Support
void ThermalPrinterComponent::set_rotation(uint8_t rotation) {
  // ESC V n - Set rotation (0=normal, 1=90°, 2=180°, 3=270°)
  this->write_bytes(ASCII_ESC, 'V', rotation & 0x03);
  delay(50); // Allow printer to process rotation command
  
  ESP_LOGD(TAG, "Set rotation to %d degrees", rotation * 90);
}

void ThermalPrinterComponent::print_rotated_text(const char* text, uint8_t rotation) {
  if (!text || strlen(text) == 0) {
    ESP_LOGW(TAG, "Empty text provided for rotated printing");
    return;
  }
  
  // Set rotation
  this->set_rotation(rotation);
  
  // Print the text
  this->print_text(text);
  
  // Reset to normal rotation
  this->set_rotation(0);
  
  // Add extra feed for rotated text
  this->feed(2);
  
  ESP_LOGI(TAG, "Printed rotated text: %d degrees", rotation * 90);
}

// QR Code Generation
void ThermalPrinterComponent::print_qr_code(const char* data, uint8_t size, uint8_t error_correction) {
  if (!data || strlen(data) == 0) {
    ESP_LOGW(TAG, "Empty data provided for QR code");
    return;
  }
  
  if (strlen(data) > 2048) {
    ESP_LOGW(TAG, "QR code data too long (max 2048 characters)");
    return;
  }
  
  ESP_LOGD(TAG, "Printing QR code: size=%d, error_correction=%d", size, error_correction);
  
  // QR Code Model 2 commands for thermal printers
  // Set QR code size (1-16, where 3 is medium)
  this->write_bytes(ASCII_GS, '(', 'k', 4, 0, 49, 65, size, 0);
  delay(10);
  
  // Set error correction level (0-3: Low, Medium, Quartile, High)
  this->write_bytes(ASCII_GS, '(', 'k', 3, 0, 49, 67, error_correction);
  delay(10);
  
  // Store QR code data
  uint16_t data_len = strlen(data);
  uint16_t total_len = data_len + 3;
  
  this->write_bytes(ASCII_GS, '(', 'k', total_len & 0xFF, (total_len >> 8) & 0xFF, 49, 80, 48);
  this->print(data);
  delay(50);
  
  // Print QR code
  this->write_bytes(ASCII_GS, '(', 'k', 3, 0, 49, 81, 48);
  delay(300); // QR codes need more processing time
  
  this->feed(2); // Add spacing after QR code
  
  // Track QR code printing (estimate 8 lines for QR + spacing)
  this->track_print_operation(data_len, 8, 2);
  
  ESP_LOGI(TAG, "QR code printed successfully");
}

// Enhanced Error Handling
PrintResult ThermalPrinterComponent::safe_print_text(const char* text) {
  if (!text || strlen(text) == 0) {
    return PrintResult::SUCCESS; // Nothing to print is success
  }
  
  // Check paper status
  if (!this->has_paper()) {
    ESP_LOGW(TAG, "Cannot print: Paper out");
    return PrintResult::PAPER_OUT;
  }
  
  // Estimate lines needed
  uint16_t estimated_lines = this->estimate_lines_for_text(text);
  
  // Check if we have enough paper
  if (!this->can_print_job(estimated_lines)) {
    ESP_LOGW(TAG, "Cannot print: Insufficient paper (need %d lines)", estimated_lines);
    return PrintResult::INSUFFICIENT_PAPER;
  }
  
  // Attempt print - simplified for existing codebase compatibility
  this->print_text(text);
  
  ESP_LOGI(TAG, "Print completed successfully");
  return PrintResult::SUCCESS;
}

// Smart Paper Management
bool ThermalPrinterComponent::can_print_job(uint16_t estimated_lines) {
  float required_mm = estimated_lines * this->line_height_mm_;
  float remaining_mm = this->paper_roll_length_ - this->get_paper_usage_mm();
  
  bool can_print = required_mm <= remaining_mm;
  
  ESP_LOGD(TAG, "Paper check: need %.1fmm, have %.1fmm remaining", 
           required_mm, remaining_mm);
  
  return can_print;
}

uint16_t ThermalPrinterComponent::estimate_lines_for_text(const char* text) {
  if (!text) return 0;
  
  uint16_t lines = 1; // At least one line
  size_t text_len = strlen(text);
  
  // Count explicit newlines
  for (size_t i = 0; i < text_len; i++) {
    if (text[i] == '\n') lines++;
  }
  
  // Estimate word wrapping (assume 32 characters per line for normal text)
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
  
  ESP_LOGD(TAG, "Estimated %d lines for %d characters", lines, text_len);
  return lines;
}

// Advanced Heat Configuration
void ThermalPrinterComponent::set_heat_config_advanced(uint8_t dots, uint8_t time, uint8_t interval, uint8_t density) {
  // Enhanced heat configuration with density control
  this->write_bytes(ASCII_ESC, '7');
  this->write_bytes(dots & 0x0F);    // Limit to 4 bits
  this->write_bytes(time & 0xFF);    // Heat time
  this->write_bytes(interval & 0xFF); // Heat interval
  
  // Set print density and break time
  this->write_bytes(ASCII_DC2, '#', (density << 4) | density);
  
  ESP_LOGD(TAG, "Set advanced heat config: dots=%d, time=%d, interval=%d, density=%d", 
           dots, time, interval, density);
}

// Status Monitoring
bool ThermalPrinterComponent::get_detailed_status(PrinterStatus* status) {
  if (!status) return false;
  
  // Initialize status with defaults
  status->paper_present = this->has_paper();
  status->cover_open = false;  // Most basic printers don't report this
  status->cutter_error = false;
  status->printer_online = true; // Assume online if we can communicate
  status->temperature_estimate = 25.0; // Room temperature
  status->last_response_time = millis();
  
  ESP_LOGD(TAG, "Basic printer status: paper=%s", 
           status->paper_present ? "OK" : "OUT");
  
  return true; // Always return true for basic status
}

// Template Printing
void ThermalPrinterComponent::print_simple_receipt(const char* business_name, const char* total) {
  ESP_LOGI(TAG, "Printing simple receipt");
  
  // Check paper availability
  if (!this->has_paper()) {
    ESP_LOGW(TAG, "Cannot print receipt: No paper");
    return;
  }
  
  // Header
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  this->print_text(business_name ? business_name : "Receipt");
  this->bold_off();
  this->feed(2);
  
  // Date line (simplified)
  this->justify('L');
  this->set_text_size(1);
  this->print_text("Date: [Current]");
  this->print_text("--------------------------------");
  this->feed(1);
  
  // Total
  if (total) {
    this->set_text_size(1);
    this->bold_on();
    this->print_two_column("TOTAL:", total, true, 'S');
    this->bold_off();
  }
  
  this->feed(3);
  
  // Footer
  this->justify('C');
  this->print_text("Thank you!");
  this->feed(4);
  
  // Reset to defaults
  this->justify('L');
  this->set_text_size(2);
}

void ThermalPrinterComponent::print_shopping_list(const char* items_string) {
  if (!items_string || strlen(items_string) == 0) {
    ESP_LOGW(TAG, "Empty shopping list");
    return;
  }
  
  ESP_LOGI(TAG, "Printing shopping list");
  
  // Header
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  this->print_text("SHOPPING LIST");
  this->bold_off();
  this->feed(2);
  
  // Simple date placeholder
  this->set_text_size(1);
  this->print_text("Date: [Today]");
  this->feed(1);
  
  this->print_text("================================");
  this->feed(1);
  
  // Print items as simple numbered list
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

// Paper Usage Prediction
float ThermalPrinterComponent::predict_paper_usage_for_job(const char* text, uint8_t text_size) {
  if (!text) return 0.0;
  
  uint16_t lines = this->estimate_lines_for_text(text);
  
  // Adjust for text size
  float size_multiplier = 1.0;
  switch (text_size) {
    case 'L': size_multiplier = 2.0; break; // Large text uses more space
    case 'M': size_multiplier = 1.5; break; // Medium text
    case 'S': 
    default:  size_multiplier = 1.0; break; // Small text
  }
  
  float estimated_mm = lines * this->line_height_mm_ * size_multiplier;
  
  ESP_LOGD(TAG, "Predicted paper usage: %.1fmm for %d lines (size multiplier: %.1f)", 
           estimated_mm, lines, size_multiplier);
  
  return estimated_mm;
}

// Configuration Validation
bool ThermalPrinterComponent::validate_config() {
  bool valid = true;
  
  // Check UART configuration
  if (!this->parent_) {
    ESP_LOGE(TAG, "UART parent not configured");
    valid = false;
  } else {
    uint32_t baud_rate = this->parent_->get_baud_rate();
    if (baud_rate != 9600 && baud_rate != 19200 && baud_rate != 38400) {
      ESP_LOGW(TAG, "Unusual baud rate: %d (recommended: 9600)", baud_rate);
    }
  }
  
  // Check paper roll configuration
  if (this->paper_roll_length_ <= 0) {
    ESP_LOGW(TAG, "Invalid paper roll length: %.1fmm", this->paper_roll_length_);
    this->paper_roll_length_ = 30000.0; // Default to 30m
  }
  
  if (this->line_height_mm_ <= 0 || this->line_height_mm_ > 10.0) {
    ESP_LOGW(TAG, "Invalid line height: %.2fmm (setting to 4.0mm)", this->line_height_mm_);
    this->line_height_mm_ = 4.0;
  }
  
  ESP_LOGD(TAG, "Configuration validation %s", valid ? "passed" : "failed");
  return valid;
}

// Startup Message
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
  
  // Simple timestamp without time component dependency
  this->print_text("System Started");
  this->feed(1);
  
  this->justify('L');
  this->set_text_size(2);
  this->feed(2);
}

// Error Recovery
void ThermalPrinterComponent::recover_from_error() {
  ESP_LOGI(TAG, "Attempting error recovery");
  
  // Clear buffers
  while (this->available()) {
    this->read();
  }
  
  // Reset printer
  this->reset();
  delay(1000);
  
  // Re-initialize
  this->wake();
  delay(500);
  
  this->set_heat_config_advanced(7, 80, 2, 4);
  this->set_default();
  
  ESP_LOGI(TAG, "Error recovery completed");
}

// Performance Monitoring
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

}  // namespace thermal_printer
}  // namespace esphome
