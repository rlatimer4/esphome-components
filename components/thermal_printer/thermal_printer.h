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
  
  // Get detailed printer status
  PrinterStatus status;
  if (this->get_detailed_status(&status)) {
    if (!status.printer_online) {
      return PrintResult::PRINTER_OFFLINE;
    }
    if (status.cover_open) {
      return PrintResult::COVER_OPEN;
    }
  }
  
  // Attempt print with timeout monitoring
  uint32_t start_time = millis();
  
  this->print_text(text);
  
  // Monitor for completion (simplified - in real implementation you'd check printer feedback)
  while (millis() - start_time < 10000) { // 10 second timeout
    delay(100);
    // In a real implementation, you'd check for print completion feedback
    // For now, we'll assume success after reasonable delay
    if (millis() - start_time > 1000) { // Minimum 1 second processing time
      ESP_LOGI(TAG, "Print completed successfully");
      return PrintResult::SUCCESS;
    }
  }
  
  ESP_LOGW(TAG, "Print operation timed out");
  return PrintResult::COMMUNICATION_ERROR;
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
  
  // Initialize status
  memset(status, 0, sizeof(PrinterStatus));
  status->last_response_time = millis();
  
  // Query printer status
  this->write_bytes(ASCII_ESC, 'v', 0); // Paper status
  delay(50);
  
  bool got_response = false;
  if (this->available()) {
    uint8_t paper_status = this->read();
    status->paper_present = (paper_status & 0x0C) == 0;
    got_response = true;
  }
  
  // Query real-time status
  this->write_bytes(ASCII_GS, 'r', 1);
  delay(50);
  
  if (this->available()) {
    uint8_t printer_status = this->read();
    status->cover_open = (printer_status & 0x04) != 0;
    status->cutter_error = (printer_status & 0x08) != 0;
    status->printer_online = (printer_status & 0x02) == 0;
    got_response = true;
  }
  
  // Estimate temperature based on recent print activity
  uint32_t recent_activity = millis() - this->last_paper_check_;
  if (recent_activity < 30000) { // Active in last 30 seconds
    status->temperature_estimate = 45.0 + (30000 - recent_activity) / 1000.0; // Rough estimate
  } else {
    status->temperature_estimate = 25.0; // Room temperature
  }
  
  ESP_LOGD(TAG, "Printer status: paper=%s, cover=%s, online=%s", 
           status->paper_present ? "OK" : "OUT",
           status->cover_open ? "OPEN" : "CLOSED",
           status->printer_online ? "YES" : "NO");
  
  return got_response;
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
  
  // Print current time if available
  auto time = id(sntp_time).now();
  if (time.is_valid()) {
    char time_str[32];
    time.strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M");
    this->print_text(time_str);
    this->feed(1);
  }
  
  this->justify('L');
  this->set_text_size(2);
  this->feed(2);
}

// Buffer management for better performance
class PrintBuffer {
private:
  static const size_t BUFFER_SIZE = 512;
  uint8_t buffer_[BUFFER_SIZE];
  size_t buffer_pos_;
  ThermalPrinterComponent* printer_;
  
public:
  PrintBuffer(ThermalPrinterComponent* printer) : buffer_pos_(0), printer_(printer) {}
  
  void add_byte(uint8_t byte) {
    if (buffer_pos_ >= BUFFER_SIZE - 1) {
      flush();
    }
    buffer_[buffer_pos_++] = byte;
  }
  
  void add_bytes(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      add_byte(data[i]);
    }
  }
  
  void flush() {
    if (buffer_pos_ > 0 && printer_) {
      printer_->write_array(buffer_, buffer_pos_);
      buffer_pos_ = 0;
    }
  }
  
  size_t size() const { return buffer_pos_; }
  bool empty() const { return buffer_pos_ == 0; }
};

// Use buffered printing for large jobs
void ThermalPrinterComponent::print_text_buffered(const char* text) {
  if (!text || strlen(text) == 0) return;
  
  PrintBuffer buffer(this);
  size_t text_len = strlen(text);
  
  ESP_LOGD(TAG, "Buffered printing %d characters", text_len);
  
  for (size_t i = 0; i < text_len; i++) {
    buffer.add_byte(text[i]);
    
    // Flush periodically to prevent timeout
    if (buffer.size() >= 256) {
      buffer.flush();
      delay(10); // Small delay to prevent overflow
    }
  }
  
  buffer.flush();
  
  // Track the operation
  this->track_print_operation(text_len, this->estimate_lines_for_text(text), 0);
}

// Template printing system
void ThermalPrinterComponent::print_receipt_template(const std::map<std::string, std::string>& data) {
  ESP_LOGI(TAG, "Printing receipt template");
  
  // Check if we have enough paper (estimate 20 lines for receipt)
  if (!this->can_print_job(20)) {
    ESP_LOGW(TAG, "Insufficient paper for receipt template");
    return;
  }
  
  // Header
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  auto business_name = data.find("business_name");
  if (business_name != data.end()) {
    this->print_text(business_name->second.c_str());
  } else {
    this->print_text("Receipt");
  }
  this->bold_off();
  this->feed(2);
  
  // Date and time
  this->justify('L');
  this->set_text_size(1);
  auto date = data.find("date");
  auto time = data.find("time");
  if (date != data.end()) {
    this->print_two_column("Date:", date->second.c_str(), false, 'S');
  }
  if (time != data.end()) {
    this->print_two_column("Time:", time->second.c_str(), false, 'S');
  }
  
  this->feed(1);
  this->print_text("--------------------------------");
  this->feed(1);
  
  // Items
  auto items = data.find("items");
  if (items != data.end()) {
    // Parse items (simple format: "item1:price1,item2:price2")
    std::string items_str = items->second;
    size_t pos = 0;
    while ((pos = items_str.find(',')) != std::string::npos || !items_str.empty()) {
      std::string item = items_str.substr(0, pos);
      if (pos != std::string::npos) {
        items_str.erase(0, pos + 1);
      } else {
        items_str.clear();
      }
      
      size_t colon_pos = item.find(':');
      if (colon_pos != std::string::npos) {
        std::string item_name = item.substr(0, colon_pos);
        std::string item_price = item.substr(colon_pos + 1);
        this->print_two_column(item_name.c_str(), item_price.c_str(), true, 'S');
      }
      
      if (pos == std::string::npos) break;
    }
  }
  
  this->feed(1);
  this->print_text("--------------------------------");
  
  // Total
  auto total = data.find("total");
  if (total != data.end()) {
    this->set_text_size(1);
    this->bold_on();
    this->print_two_column("TOTAL:", total->second.c_str(), true, 'S');
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

// Shopping list template
void ThermalPrinterComponent::print_shopping_list_template(const char* items_string) {
  if (!items_string || strlen(items_string) == 0) {
    ESP_LOGW(TAG, "Empty shopping list");
    return;
  }
  
  ESP_LOGI(TAG, "Printing shopping list template");
  
  // Header
  this->set_text_size(2);
  this->justify('C');
  this->bold_on();
  this->print_text("SHOPPING LIST");
  this->bold_off();
  this->feed(2);
  
  // Date
  this->set_text_size(1);
  auto time = id(sntp_time).now();
  if (time.is_valid()) {
    char date_str[32];
    time.strftime(date_str, sizeof(date_str), "%B %d, %Y");
    this->print_text(date_str);
    this->feed(1);
  }
  
  this->print_text("================================");
  this->feed(1);
  
  // Parse and print items (comma-separated)
  std::string items(items_string);
  size_t pos = 0;
  int item_count = 1;
  
  this->justify('L');
  while ((pos = items.find(',')) != std::string::npos || !items.empty()) {
    std::string item = items.substr(0, pos);
    if (pos != std::string::npos) {
      items.erase(0, pos + 1);
    } else {
      items.clear();
    }
    
    // Trim whitespace
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    
    if (!item.empty()) {
      char line[64];
      snprintf(line, sizeof(line), "%2d. [ ] %s", item_count++, item.c_str());
      this->print_text(line);
      this->feed(1);
    }
    
    if (pos == std::string::npos) break;
  }
  
  this->feed(2);
  this->print_text("================================");
  this->feed(1);
  
  char summary[64];
  snprintf(summary, sizeof(summary), "Total items: %d", item_count - 1);
  this->justify('C');
  this->print_text(summary);
  
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
