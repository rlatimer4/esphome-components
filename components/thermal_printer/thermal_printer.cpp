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
  ESP_LOGCONFIG(TAG, "Setting up Enhanced Thermal Printer with Queue System...");
  
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
  
  ESP_LOGCONFIG(TAG, "Printer setup complete - DTR: %s, Queue: ENABLED", 
                this->is_dtr_enabled() ? "ENABLED" : "Disabled");
  ESP_LOGCONFIG(TAG, "Queue settings: max_size=%d, delay=%dms", 
                this->max_queue_size_, this->print_delay_ms_);
}

void ThermalPrinterComponent::loop() {
  // ===== NEW: PROCESS PRINT QUEUE =====
  if (this->auto_process_queue_) {
    this->process_print_queue();
  }
  
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
  ESP_LOGCONFIG(TAG, "Enhanced Thermal Printer with Queue System:");
  ESP_LOGCONFIG(TAG, "  DTR: %s", this->is_dtr_enabled() ? "ENABLED" : "Disabled");
  if (this->is_dtr_enabled()) {
    ESP_LOGCONFIG(TAG, "  DTR Pin: GPIO%d", this->dtr_pin_->get_pin());
    ESP_LOGCONFIG(TAG, "  DTR Timeouts: %u", this->dtr_timeout_count_);
  }
  ESP_LOGCONFIG(TAG, "  Queue Size: %d/%d", this->get_queue_length(), this->max_queue_size_);
  ESP_LOGCONFIG(TAG, "  Print Delay: %dms", this->print_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Auto Process: %s", this->auto_process_queue_ ? "ON" : "OFF");
  ESP_LOGCONFIG(TAG, "  Jobs Processed: %u", this->total_jobs_processed_);
  ESP_LOGCONFIG(TAG, "  Jobs Dropped: %u", this->jobs_dropped_);
}

// ===== QUEUE MANAGEMENT METHODS =====

PrintResult ThermalPrinterComponent::queue_print_job(const PrintJob& job) {
  // Check if queue is full
  if (this->print_queue_.size() >= this->max_queue_size_) {
    ESP_LOGW(TAG, "Print queue full (%d jobs), dropping oldest job", this->max_queue_size_);
    this->print_queue_.pop();
    this->jobs_dropped_++;
  }
  
  // Add timestamp
  PrintJob queued_job = job;
  queued_job.timestamp = millis();
  
  // Add to queue (priority queue could be implemented here)
  this->print_queue_.push(queued_job);
  
  ESP_LOGI(TAG, "Print job queued (type: %d, priority: %d, queue size: %d)", 
           job.type, job.priority, this->print_queue_.size());
  
  return PrintResult::SUCCESS;
}

void ThermalPrinterComponent::process_print_queue() {
  // Don't process if conditions aren't met
  if (!this->should_process_queue()) {
    return;
  }
  
  if (this->print_queue_.empty()) {
    return;
  }
  
  // Mark printer as busy
  this->printer_busy_ = true;
  this->current_job_start_time_ = millis();
  
  // Get next job
  PrintJob job = this->print_queue_.front();
  this->print_queue_.pop();
  
  ESP_LOGI(TAG, "Processing print job (type: %d, queue remaining: %d)", 
           job.type, this->print_queue_.size());
  
  // Execute the job
  this->execute_print_job(job);
  
  // Update timing and statistics
  uint32_t job_duration = millis() - this->current_job_start_time_;
  this->update_queue_statistics(job_duration);
  
  this->last_print_time_ = millis();
  this->total_jobs_processed_++;
  this->printer_busy_ = false;
}

bool ThermalPrinterComponent::should_process_queue() {
  // Don't process if printer is busy
  if (this->printer_busy_) {
    return false;
  }
  
  // Don't process if not enough time has passed since last job
  if ((millis() - this->last_print_time_) < this->print_delay_ms_) {
    return false;
  }
  
  // Don't process if no paper
  if (!this->has_paper()) {
    static uint32_t last_paper_warning = 0;
    if (millis() - last_paper_warning > 30000) { // Warn every 30 seconds
      ESP_LOGW(TAG, "Cannot process print queue: No paper (queue size: %d)", this->print_queue_.size());
      last_paper_warning = millis();
    }
    return false;
  }
  
  return true;
}

void ThermalPrinterComponent::execute_print_job(const PrintJob& job) {
  switch (job.type) {
    case PrintJob::TEXT:
      this->set_text_size(job.param1);
      this->justify(job.param2 == 0 ? 'L' : (job.param2 == 1 ? 'C' : 'R'));
      this->bold_on(job.param3);
      this->print_text(job.data1.c_str());
      this->bold_off();
      this->justify('L');
      break;
      
    case PrintJob::TWO_COLUMN:
      this->print_two_column(job.data1.c_str(), job.data2.c_str(), job.param3, 
                           job.param1 == 1 ? 'S' : (job.param1 == 2 ? 'M' : 'L'));
      break;
      
    case PrintJob::BARCODE:
      this->print_barcode(job.param1, job.data1.c_str());
      break;
      
    case PrintJob::QR_CODE:
      this->print_qr_code(job.data1.c_str(), job.param1, job.param2);
      break;
      
    case PrintJob::FEED_PAPER:
      this->feed(job.param1);
      break;
      
    case PrintJob::SEPARATOR:
      this->justify('C');
      this->print_text("================================");
      this->justify('L');
      this->feed(1);
      break;
      
    case PrintJob::TABLE_ROW:
      if (job.param3) { // Header row
        this->bold_on();
      }
      this->print_table_row(job.data1.c_str(), job.data2.c_str(), 
                          job.data3.empty() ? nullptr : job.data3.c_str());
      if (job.param3) {
        this->bold_off();
      }
      break;
      
    case PrintJob::ROTATED_TEXT:
      this->print_rotated_text(job.data1.c_str(), job.param1);
      break;
      
    default:
      ESP_LOGW(TAG, "Unknown job type: %d", job.type);
      break;
  }
}

void ThermalPrinterComponent::clear_print_queue() {
  // Clear the queue by swapping with empty queue
  std::queue<PrintJob> empty_queue;
  this->print_queue_.swap(empty_queue);
  ESP_LOGI(TAG, "Print queue cleared");
}

void ThermalPrinterComponent::update_queue_statistics(uint32_t job_duration) {
  this->total_processing_time_ += job_duration;
}

float ThermalPrinterComponent::get_average_job_time() const {
  if (this->total_jobs_processed_ == 0) {
    return 0.0f;
  }
  return (float)this->total_processing_time_ / this->total_jobs_processed_;
}

// ===== CONVENIENCE QUEUE METHODS =====

PrintResult ThermalPrinterComponent::queue_text(const char* text, uint8_t size, uint8_t align, bool bold, uint8_t priority) {
  PrintJob job = this->create_text_job(text, size, align, bold, priority);
  return this->queue_print_job(job);
}

PrintResult ThermalPrinterComponent::queue_two_column(const char* left, const char* right, bool dots, char size, uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::TWO_COLUMN;
  job.data1 = left;
  job.data2 = right;
  job.param1 = (size == 'S') ? 1 : (size == 'M') ? 2 : 3;
  job.param3 = dots;
  job.priority = priority;
  return this->queue_print_job(job);
}

PrintResult ThermalPrinterComponent::queue_barcode(uint8_t type, const char* data, uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::BARCODE;
  job.data1 = data;
  job.param1 = type;
  job.priority = priority;
  return this->queue_print_job(job);
}

PrintResult ThermalPrinterComponent::queue_qr_code(const char* data, uint8_t size, uint8_t error_correction, uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::QR_CODE;
  job.data1 = data;
  job.param1 = size;
  job.param2 = error_correction;
  job.priority = priority;
  return this->queue_print_job(job);
}

PrintResult ThermalPrinterComponent::queue_separator(uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::SEPARATOR;
  job.priority = priority;
  return this->queue_print_job(job);
}

PrintResult ThermalPrinterComponent::queue_feed(uint8_t lines, uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::FEED_PAPER;
  job.param1 = lines;
  job.priority = priority;
  return this->queue_print_job(job);
}

PrintJob ThermalPrinterComponent::create_text_job(const char* text, uint8_t size, uint8_t align, bool bold, uint8_t priority) {
  PrintJob job;
  job.type = PrintJob::TEXT;
  job.data1 = text;
  job.param1 = size;
  job.param2 = align;
  job.param3 = bold;
  job.priority = priority;
  return job;
}

// ===== EMERGENCY/IMMEDIATE PRINTING =====

PrintResult ThermalPrinterComponent::print_immediate(const char* text, uint8_t size, uint8_t align, bool bold) {
  if (this->printer_busy_) {
    ESP_LOGW(TAG, "Cannot print immediately: printer busy");
    return PrintResult::PRINTER_OFFLINE;
  }
  
  if (!this->has_paper()) {
    ESP_LOGW(TAG, "Cannot print immediately: no paper");
    return PrintResult::PAPER_OUT;
  }
  
  ESP_LOGI(TAG, "Emergency print: %s", text);
  
  // Mark as busy to prevent queue processing
  this->printer_busy_ = true;
  
  // Execute immediately
  this->set_text_size(size);
  this->justify(align == 0 ? 'L' : (align == 1 ? 'C' : 'R'));
  this->bold_on(bold);
  this->print_text(text);
  this->bold_off();
  this->justify('L');
  
  // Reset busy flag
  this->printer_busy_ = false;
  this->last_print_time_ = millis();
  
  return PrintResult::SUCCESS;
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

void ThermalPrinterComponent::test_page() {
  this->write_byte_with_flow_control(ASCII_DC2);
  this->write_byte_with_flow_control('T');
  this->track_print_operation(0, 10, 0); // Estimate test page usage
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

void ThermalPrinterComponent::normal() {
  this->write_byte_with_flow_control(ASCII_ESC);
  this->write_byte_with_flow_control('!');
  this->write_byte_with_flow_control(0);
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
  this->print_barcode((int)type, text);
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
    ESP_LOGW(TAG, "Cannot print: Insufficient paper (need %d lines)", estimated_lines);
    return PrintResult::INSUFFICIENT_PAPER;
  }
  
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
  
  ESP_LOGD(TAG, "Estimated %d lines for %d characters", lines, text_len);
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
  
  float estimated_mm = lines * this->line_height_mm_ * size_multiplier;
  
  ESP_LOGD(TAG, "Predicted paper usage: %.1fmm for %d lines (size multiplier: %.1f)", 
           estimated_mm, lines, size_multiplier);
  
  return estimated_mm;
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
  
  ESP_LOGD(TAG, "Detailed printer status: paper=%s, dtr=%s", 
           status->paper_present ? "OK" : "OUT",
           status->dtr_ready ? "READY" : "BUSY");
  
  return true;
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
  this->print_text("Queue System Ready!");
  this->feed(1);
  
  this->print_text("System Started");
  this->feed(1);
  
  this->justify('L');
  this->set_text_size(2);
  this->feed(2);
}

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
  ESP_LOGI(TAG, "  Queue stats: processed=%u, dropped=%u, avg_time=%.1fms", 
           this->total_jobs_processed_, this->jobs_dropped_, this->get_average_job_time());
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

// ===== MISSING QUEUE CONFIGURATION METHODS =====

void ThermalPrinterComponent::set_max_queue_size(uint8_t max_size) {
  this->max_queue_size_ = max_size;
  ESP_LOGI(TAG, "Max queue size set to %d", max_size);
}

void ThermalPrinterComponent::set_print_delay(uint32_t delay_ms) {
  this->print_delay_ms_ = delay_ms;
  ESP_LOGI(TAG, "Print delay set to %dms", delay_ms);
}

void ThermalPrinterComponent::enable_auto_queue_processing(bool enable) {
  this->auto_process_queue_ = enable;
  ESP_LOGI(TAG, "Auto queue processing %s", enable ? "enabled" : "disabled");
}

uint32_t ThermalPrinterComponent::get_total_jobs_processed() const {
  return this->total_jobs_processed_;
}

uint32_t ThermalPrinterComponent::get_jobs_dropped() const {
  return this->jobs_dropped_;
}

}  // namespace thermal_printer
}  // namespace esphome
