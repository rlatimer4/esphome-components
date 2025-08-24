// Add these new methods to your thermal_printer.h file:

// In the public section of ThermalPrinterComponent class:
void set_rotation(uint8_t rotation);
void print_rotated_text(const char* text, uint8_t rotation = 1);
void print_qr_code(const char* data, uint8_t size = 3, uint8_t error_correction = 1);
void print_qr_code_with_label(const char* data, const char* label, uint8_t size = 3);
PrintResult safe_print_text(const char* text);
bool can_print_job(uint16_t estimated_lines);
uint16_t estimate_lines_for_text(const char* text);
void set_heat_config_advanced(uint8_t dots, uint8_t time, uint8_t interval, uint8_t density = 4);
bool get_detailed_status(PrinterStatus* status);

// Add these enum and struct definitions before the class:
enum class PrintResult {
  SUCCESS,
  PAPER_OUT,
  COVER_OPEN,
  COMMUNICATION_ERROR,
  INSUFFICIENT_PAPER,
  PRINTER_OFFLINE
};

struct PrinterStatus {
  bool paper_present;
  bool cover_open;
  bool cutter_error;
  bool printer_online;
  float temperature_estimate;
  uint32_t last_response_time;
};

// Add these new methods to your thermal_printer.cpp file:

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

void ThermalPrinterComponent::print_qr_code_with_label(const char* data, const char* label, uint8_t size) {
  if (label && strlen(label) > 0) {
    // Print label above QR code
    this->justify('C');
    this->set_text_size(1); // Small text for label
    this->print_text(label);
    this->feed(1);
  }
  
  // Print QR code
  this->print_qr_code(data, size, 1); // Medium error correction
  
  // Reset formatting
  this->justify('L');
  this->set_text_size(2);
}

// Note: Add these enum/struct definitions to thermal_printer.h before the class declaration:
/*
enum class PrintResult {
  SUCCESS,
  PAPER_OUT,
  COVER_OPEN,
  COMMUNICATION_ERROR,
  INSUFFICIENT_PAPER,
  PRINTER_OFFLINE
};

struct PrinterStatus {
  bool paper_present;
  bool cover_open;
  bool cutter_error;
  bool printer_online;
  float temperature_estimate;
  uint32_t last_response_time;
};
*/

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

// Enhanced bitmap printing with better error handling
void ThermalPrinterComponent::print_bitmap_safe(const uint8_t* bitmap, uint16_t width, uint16_t height) {
  if (!bitmap) {
    ESP_LOGW(TAG, "Null bitmap provided");
    return;
  }
  
  if (width > 384 || height > 1000) {
    ESP_LOGW(TAG, "Bitmap too large: %dx%d (max 384x1000)", width, height);
    return;
  }
  
  ESP_LOGI(TAG, "Printing bitmap: %dx%d", width, height);
  
  uint16_t bytes_per_line = (width + 7) / 8;
  
  // Check if we have enough paper
  uint16_t estimated_lines = (height + 7) / 8; // 8 dots per line
  if (!this->can_print_job(estimated_lines)) {
    ESP_LOGW(TAG, "Insufficient paper for bitmap");
    return;
  }
  
  for (uint16_t y = 0; y < height; y += 8) {
    // ESC * m nL nH - Bit image mode
    this->write_bytes(ASCII_ESC, '*', 0, bytes_per_line & 0xFF, (bytes_per_line >> 8) & 0xFF);
    
    for (uint16_t x = 0; x < bytes_per_line; x++) {
      uint8_t byte_val = 0;
      for (uint8_t bit = 0; bit < 8 && (y + bit) < height; bit++) {
        uint16_t pixel_index = ((y + bit) * width + x * 8) / 8;
        if (pixel_index < (width * height + 7) / 8) {
          if (bitmap[pixel_index] & (1 << (7 - (x * 8) % 8))) {
            byte_val |= (1 << (7 - bit));
          }
        }
      }
      this->write_byte(byte_val);
    }
    this->write_byte('\n');
    
    // Add small delay to prevent buffer overflow
    if (y % 64 == 0) delay(10);
  }
  
  this->feed(2);
  this->track_print_operation(0, estimated_lines, 2);
  
  ESP_LOGI(TAG, "Bitmap printed successfully");
}

// Paper usage prediction
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

// Configuration validation
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

// Enhanced setup with better initialization sequence
void ThermalPrinterComponent::setup_enhanced() {
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

// Remove the PrintBuffer class and buffered printing methods as they're too complex
// for integration with existing codebase. Keep the concept for future implementation.

// Remove template functions that require complex parsing
// These would be better implemented as simple string-based functions

// Simplified receipt printing function
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
  this->feed(1);
  
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

// Simplified shopping list function
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

// Enhanced error recovery
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

// Performance monitoring
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
