void ThermalPrinterComponent::timeout_wait() {
  if (this->is_dtr_enabled()) {
    // Hardware handshaking: wait for DTR signal
    uint32_t start_time = millis();
    uint32_t timeout_ms = 5000; // 5 second timeout
    
    while (!this->dtr_ready()) {
      if (millis() - start_time > timeout_ms) {
        this->dtr_timeout_count_++;
        ESP_LOGW(TAG, "DTR timeout after %ums (total timeouts: %u)", timeout_ms, this->dtr_timeout_count_);
        break;
      }
      delay(1); // Small delay to prevent busy waiting
    }
    
    this->dtr_waits_++;
    
  } else {
    // Software timing: wait for calculated time
    while (micros() < this->resume_time_micros_) {
      // Busy wait for precise timing
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
    return true; // Always "ready" if DTR not enabled
  }
  
  // DTR is active LOW - printer pulls it low when ready for data
  return !this->dtr_pin_->digital_read();
}

void ThermalPrinterComponent::wait_for_printer_ready(uint32_t timeout_ms) {
  if (!this->is_dtr_enabled()) {
    // Fallback to fixed delay
    delay(timeout_ms / 10); // Use 1/10th of timeout as delay
    return;
  }
  
  uint32_t start_time = millis();
  
  while (!this->dtr_ready()) {
    if (millis() - start_time > timeout_ms) {
      this->dtr_timeout_count_++;
      ESP_LOGW(TAG, "DTR wait timeout after %ums", timeout_ms);
      break;
    }
    delay(1);
  }
}

// ===== ENHANCED WRITE METHODS WITH DTR FLOW CONTROL =====

void ThermalPrinterComponent::write_byte_with_flow_control(uint8_t byte) {
  this->timeout_wait(); // Wait for printer ready
  this->write_byte(byte); // Send byte
  this->total_bytes_sent_++;
  
  // Set timeout for next operation
  if (this->is_dtr_enabled()) {
    // With DTR, printer will signal when ready - minimal delay
    this->timeout_set(this->byte_time_micros_ / 4); // Quarter byte time safety margin
  } else {
    // Without DTR, use conservative timing
    this->timeout_set(this->byte_time_micros_ * 2); // Double byte time for safety
  }
}

void ThermalPrinterComponent::write_bytes_with_flow_control(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    this->write_byte_with_flow_control(data[i]);
  }
}

// ===== UPDATED PRINT INTERFACE WITH DTR SUPPORT =====

size_t ThermalPrinterComponent::write(uint8_t c) {
  this->write_byte_with_flow_control(c);
  
  // Track character printing
  this->characters_printed_++;
  if (c == '\n') {
    this->lines_printed_++;
    
    // Line feeds take longer - set appropriate timeout
    if (this->is_dtr_enabled()) {
      this->timeout_set(this->dot_feed_time_micros_ * 8); // ~2.7ms for line feed
    } else {
      this->timeout_set(this->dot_feed_time_micros_ * 16); // Conservative timing
    }
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

// ===== UPDATED THERMAL PRINTER CONTROL METHODS WITH DTR =====

void ThermalPrinterComponent::wake() {
  ESP_LOGD(TAG, "Waking printer...");
  this->write_byte_with_flow_control(255);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(3000); // 3 second timeout for wake
  } else {
    delay(50);
  }
  
  this->set_heat_config(this->heat_dots_, this->heat_time_, this->heat_interval_);
}

void ThermalPrinterComponent::sleep() {
  ESP_LOGD(TAG, "Putting printer to sleep...");
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('8');
  this->write_byte_with_flow_control(0);
  this->write_byte_with_flow_control(0);
}

void ThermalPrinterComponent::reset() {
  ESP_LOGD(TAG, "Resetting printer...");
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('@');
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(5000); // Reset takes longer
  } else {
    delay(500);
  }
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
  ESP_LOGD(TAG, "Printing test page...");
  this->write_byte_with_flow_control(ASCII_DC2);
  this->write_byte_with_flow_control('T');
  
  // Test pages take significant time
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(10000); // 10 second timeout for test page
  } else {
    delay(2000);
  }
  
  this->track_print_operation(0, 10, 0); // Estimate test page usage
}

void ThermalPrinterComponent::set_heat_config(uint8_t dots, uint8_t time, uint8_t interval) {
  ESP_LOGD(TAG, "Setting heat config: dots=%u, time=%u, interval=%u", dots, time, interval);
  
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('7');
  this->write_byte_with_flow_control(dots);
  this->write_byte_with_flow_control(time);
  this->write_byte_with_flow_control(interval);
  
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('D');
  this->write_byte_with_flow_control(4);
  this->write_byte_with_flow_control(8);
  this->write_byte_with_flow_control(12);
  this->write_byte_with_flow_control(16);
  this->write_byte_with_flow_control(20);
  this->write_byte_with_flow_control(24);
  this->write_byte_with_flow_control(28);
  this->write_byte_with_flow_control(0);
  
  // Store heat configuration
  this->heat_dots_ = dots;
  this->heat_time_ = time;
  this->heat_interval_ = interval;
}

void ThermalPrinterComponent::normal() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(0);
}

void ThermalPrinterComponent::inverse_on(bool state) {
  this->write_byte_with_flow_control(ASCII_GS);
  this->write_byte_with_flow_control('B');
  this->write_byte_with_flow_control(state ? 1 : 0);
}

void ThermalPrinterComponent::inverse_off() {
  this->inverse_on(false);
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
  ESP_LOGD(TAG, "Feeding %u lines", x);
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('d');
  this->write_byte_with_flow_control(x);
  
  // Paper feed operations take time proportional to lines
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(x * 100 + 1000); // 100ms per line + 1s base
  } else {
    delay(x * 50 + 200); // Conservative timing
  }
  
  this->track_print_operation(0, 0, x);
}

void ThermalPrinterComponent::feed_rows(uint8_t rows) {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('J');
  this->write_byte_with_flow_control(rows);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(rows * 10 + 500);
  } else {
    delay(rows * 5 + 100);
  }
  
  this->track_print_operation(0, 0, rows);
}

void ThermalPrinterComponent::print_barcode(const char *text, uint8_t type) {
  ESP_LOGD(TAG, "Printing barcode: type=%u, data='%s'", type, text);
  
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
  
  // Barcodes take significant time to print
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(5000); // 5 second timeout for barcode
  } else {
    delay(300);
  }
  
  // Track barcode printing (estimate 3 lines)
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

// ===== ENHANCED METHODS WITH DTR SUPPORT =====

void ThermalPrinterComponent::print_text(const char *text) {
  if (!text || strlen(text) == 0) {
    ESP_LOGW(TAG, "Empty text provided for printing");
    return;
  }
  
  size_t len = strlen(text);
  ESP_LOGD(TAG, "Printing text: length=%u", len);
  
  this->print(text);
  
  // Count newlines for line tracking
  uint8_t newlines = 0;
  for (size_t i = 0; i < len; i++) {
    if (text[i] == '\n') newlines++;
  }
  
  this->track_print_operation(len, newlines, 0);
}

// 90-Degree Rotation Support - Updated with DTR
void ThermalPrinterComponent::set_rotation(uint8_t rotation) {
  ESP_LOGD(TAG, "Setting rotation to %d degrees", rotation * 90);
  
  // ESC V n - Set rotation (0=normal, 1=90°, 2=180°, 3=270°)
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('V');
  this->write_byte_with_flow_control(rotation & 0x03);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(500); // Rotation command processing time
  } else {
    delay(50);
  }
}

void ThermalPrinterComponent::print_rotated_text(const char* text, uint8_t rotation) {
  if (!text || strlen(text) == 0) {
    ESP_LOGW(TAG, "Empty text provided for rotated printing");
    return;
  }
  
  // CRITICAL: Limit text length for stability with DTR handshaking
  if (strlen(text) > 20) {
    ESP_LOGW(TAG, "Text too long for rotation (max 20 chars): truncating");
    // Don't return - just log warning and continue with truncated text
  }
  
  ESP_LOGI(TAG, "Printing rotated text: '%s', rotation=%d, DTR=%s", 
           text, rotation, this->is_dtr_enabled() ? "enabled" : "disabled");
  
  if (rotation == 1) {
    // 90-degree rotation: Use ONLY small text size for reliability
    this->set_text_size(1);  // Force small size
    this->justify('C');      // Center alignment
    this->set_rotation(1);   // Enable 90-degree rotation
    
    // Print each character with proper DTR handshaking
    size_t text_len = strlen(text);
    size_t max_chars = text_len > 20 ? 20 : text_len; // Limit to 20 chars
    
    for (size_t i = 0; i < max_chars; i++) {
      if (text[i] == ' ') {
        // Visual space indicator
        this->print_text("·");
        this->feed(1);
      } else if (text[i] == '\n') {
        // Extra space for paragraph breaks
        this->feed(3);
      } else {
        // Print each character rotated
        char single_char[2] = {text[i], '\0'};
        this->print_text(single_char);
        this->feed(2);  // Spacing between rotated letters
        
        // CRITICAL: With DTR enabled, wait for printer to be ready
        // This prevents the buffer overflow that causes missing letters
        if (this->is_dtr_enabled()) {
          this->wait_for_printer_ready(2000); // 2 second timeout per character
        } else {
          delay(100); // Conservative delay without DTR
        }
      }
    }
    
    // Reset everything
    this->set_rotation(0);   // Turn off rotation
    this->justify('L');      // Reset alignment  
    this->set_text_size(2);  // Reset to medium size
    this->feed(3);           // Final spacing
    
  } else {
    // Other rotations or normal text
    this->print_text(text);
    this->feed(1);
  }
  
  ESP_LOGI(TAG, "Rotated text printing complete");
}

// QR Code Generation - Updated with DTR flow control
void ThermalPrinterComponent::print_qr_code(const char* data, uint8_t size, uint8_t error_correction) {
  if (!data || strlen(data) == 0) {
    ESP_LOGW(TAG, "Empty data provided for QR code");
    return;
  }
  
  if (strlen(data) > 2048) {
    ESP_LOGW(TAG, "QR code data too long (max 2048 characters)");
    return;
  }
  
  ESP_LOGD(TAG, "Printing QR code: size=%d, error_correction=%d, length=%u", 
           size, error_correction, strlen(data));
  
  // QR Code Model 2 commands - all with DTR flow control
  // Set QR code size (1-16, where 3 is medium)
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
  
  // Set error correction level (0-3: Low, Medium, Quartile, High)
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
  
  // Store QR code data
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
  
  // Send QR code data with flow control
  for (size_t i = 0; i < data_len; i++) {
    this->write_byte_with_flow_control(data[i]);
  }
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(3000); // Data processing time
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
  
  // QR codes take significant time to generate and print
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(10000); // 10 second timeout for QR generation
  } else {
    delay(300);
  }
  
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
  
  // Attempt print - DTR will handle timing automatically
  this->print_text(text);
  
  ESP_LOGI(TAG, "Print completed successfully");
  return PrintResult::SUCCESS;
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

//#include "thermal_printer.h"
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
  ESP_LOGCONFIG(TAG, "Setting up Enhanced Thermal Printer with DTR Handshaking...");
  
  // Validate configuration first
  if (!this->validate_config()) {
    ESP_LOGE(TAG, "Configuration validation failed");
    this->mark_failed();
    return;
  }
  
  // Initialize DTR pin if configured
  if (this->dtr_pin_ != nullptr) {
    this->dtr_pin_->setup();
    this->dtr_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    
    // Test DTR pin connectivity
    bool initial_dtr_state = this->dtr_pin_->digital_read();
    ESP_LOGI(TAG, "DTR pin configured on GPIO%d, initial state: %s", 
             this->dtr_pin_->get_pin(), initial_dtr_state ? "HIGH" : "LOW");
    
    // Validate DTR is working (should be HIGH when printer is not ready initially)
    if (!initial_dtr_state) {
      ESP_LOGW(TAG, "DTR pin is LOW at startup - check wiring or printer may be ready");
    }
  }
  
  // Initialize DTR timing calculations
  this->initialize_dtr_timings();
  
  // Extended initialization wait for printer boot
  delay(1000);
  
  // Clear any pending data
  while (this->available()) {
    this->read();
  }
  
  // Wake up printer with DTR-aware communication
  bool printer_responded = false;
  for (int attempts = 0; attempts < 3 && !printer_responded; attempts++) {
    ESP_LOGD(TAG, "Wake attempt %d/3", attempts + 1);
    
    this->wake();
    
    if (this->is_dtr_enabled()) {
      // With DTR: Wait for printer to signal ready
      this->wait_for_printer_ready(2000); // 2 second timeout
    } else {
      // Without DTR: Use conservative delay
      delay(500);
    }
    
    // Test communication with status query
    this->write_byte_with_flow_control(ASCII_ESC);
    this->write_byte_with_flow_control('v');
    this->write_byte_with_flow_control(0);
    
    if (this->is_dtr_enabled()) {
      this->wait_for_printer_ready(1000);
    } else {
      delay(100);
    }
    
    if (this->available()) {
      this->read(); // Clear response
      printer_responded = true;
      ESP_LOGI(TAG, "Printer communication established on attempt %d", attempts + 1);
    } else {
      ESP_LOGW(TAG, "No response from printer on attempt %d", attempts + 1);
    }
  }
  
  if (!printer_responded) {
    ESP_LOGE(TAG, "Failed to establish printer communication");
    // Don't fail completely - might work anyway
  }
  
  // Configure printer with conservative heat settings for reliability
  this->set_heat_config_advanced(this->heat_dots_, this->heat_time_, this->heat_interval_, 4);
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(1000);
  } else {
    delay(100);
  }
  
  // Set comprehensive defaults
  this->set_default();
  
  if (this->is_dtr_enabled()) {
    this->wait_for_printer_ready(1000);
  } else {
    delay(100);
  }
  
  // Load usage data from flash
  this->load_usage_from_flash();
  
  // Initial paper check
  bool has_paper = this->has_paper();
  this->paper_status_ = has_paper;
  
  ESP_LOGCONFIG(TAG, "Enhanced Thermal Printer setup complete");
  ESP_LOGCONFIG(TAG, "  DTR Handshaking: %s", this->is_dtr_enabled() ? "ENABLED" : "Disabled");
  if (this->is_dtr_enabled()) {
    ESP_LOGCONFIG(TAG, "  DTR Pin: GPIO%d", this->dtr_pin_->get_pin());
    ESP_LOGCONFIG(TAG, "  Byte Time: %u µs", this->byte_time_micros_);
  }
  ESP_LOGCONFIG(TAG, "  Paper status: %s", has_paper ? "Present" : "Out");
  ESP_LOGCONFIG(TAG, "  Paper usage: %.1f mm (%.1f%% of %.1fm roll)", 
                this->get_paper_usage_mm(), 
                this->get_paper_usage_percent(),
                this->paper_roll_length_ / 1000.0);
  ESP_LOGCONFIG(TAG, "  Lines printed: %u", this->lines_printed_);
  ESP_LOGCONFIG(TAG, "  Characters printed: %u", this->characters_printed_);
  
  // Print startup message if paper is available and printer responded
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
  
  // Update performance statistics periodically
  static uint32_t last_stats_update = 0;
  if (millis() - last_stats_update > 60000) { // Every minute
    last_stats_update = millis();
    this->update_performance_stats();
  }
}

void ThermalPrinterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Enhanced Thermal Printer:");
  ESP_LOGCONFIG(TAG, "  Baud Rate: %d", this->parent_->get_baud_rate());
  ESP_LOGCONFIG(TAG, "  DTR Handshaking: %s", this->is_dtr_enabled() ? "ENABLED" : "Disabled");
  if (this->is_dtr_enabled()) {
    ESP_LOGCONFIG(TAG, "    DTR Pin: GPIO%d", this->dtr_pin_->get_pin());
    ESP_LOGCONFIG(TAG, "    DTR Timeouts: %u", this->dtr_timeout_count_);
    ESP_LOGCONFIG(TAG, "    Total Bytes Sent: %u", this->total_bytes_sent_);
    ESP_LOGCONFIG(TAG, "    DTR Waits: %u, Timeout Waits: %u", this->dtr_waits_, this->timeout_waits_);
  }
  ESP_LOGCONFIG(TAG, "  Heat Configuration: dots=%u, time=%u, interval=%u", 
                this->heat_dots_, this->heat_time_, this->heat_interval_);
  ESP_LOGCONFIG(TAG, "  Lines Printed: %u", this->lines_printed_);
  ESP_LOGCONFIG(TAG, "  Characters Printed: %u", this->characters_printed_);
  ESP_LOGCONFIG(TAG, "  Paper Usage: %.1f mm (%.1f%%)", 
                this->get_paper_usage_mm(), this->get_paper_usage_percent());
}

// ===== DTR HANDSHAKING CORE METHODS =====

void ThermalPrinterComponent::initialize_dtr_timings() {
  // Calculate timing based on baud rate (from Adafruit library)
  uint32_t baud_rate = this->parent_ ? this->parent_->get_baud_rate() : 19200;
  
  // Time per byte = (bits_per_byte / baud_rate) * 1,000,000 microseconds
  // 10 bits per byte (8 data + 1 start + 1 stop)
  this->byte_time_micros_ = (10 * 1000000) / baud_rate;
  
  // Conservative estimates for thermal printer mechanics (from Adafruit library)
  this->dot_print_time_micros_ = 33;  // Time to print one pixel line
  this->dot_feed_time_micros_ = 333;  // Time to feed one pixel line
  
  ESP_LOGD(TAG, "DTR Timings initialized: byte_time=%uµs, dot_print=%uµs, dot_feed=%uµs", 
           this->byte_time_micros_, this->dot_print_time_micros_, this->dot_feed_time_micros_);
}

void ThermalPrinterComponent::timeout_wait() {
  if (this->is_dtr_enabled()) {
    // Hardware handshaking: wait for DTR signal
    uint32_t start_time = millis();
    uint32_t timeout_ms = 5000; // 5 second timeout
    
    while (!this->dtr_ready()) {
      if (millis() - start_time > timeout_ms) {
        this->dtr_timeout_count_++;
        ESP_LOGW(TAG, "DTR timeout after %ums (total timeouts
