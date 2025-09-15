#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"
#include "esphome/core/gpio.h"
#include <queue>

namespace esphome {
namespace thermal_printer {

// Simple Print base class for ESPHome compatibility
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) = 0;
  
  size_t print(const char *str) {
    return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
  }
  
  size_t println(const char *str) {
    size_t n = print(str);
    n += write('\n');
    return n;
  }
  
  size_t println() {
    return write('\n');
  }
};

// Barcode types
enum BarCodeType {
  UPC_A = 0,
  UPC_E = 1,
  EAN13 = 2,
  EAN8 = 3,
  CODE39 = 4,
  ITF = 5,
  CODABAR = 6,
  CODE93 = 7,
  CODE128 = 8
};

// Enhanced result types and status structures
enum class PrintResult {
  SUCCESS,
  PAPER_OUT,
  COVER_OPEN,
  COMMUNICATION_ERROR,
  INSUFFICIENT_PAPER,
  PRINTER_OFFLINE,
  DTR_TIMEOUT,
  QUEUE_FULL
};

struct PrinterStatus {
  bool paper_present;
  bool cover_open;
  bool cutter_error;
  bool printer_online;
  bool dtr_ready;
  float temperature_estimate;
  uint32_t last_response_time;
  uint32_t dtr_timeouts;
};

// ===== NEW: PRINT JOB QUEUE SYSTEM =====
struct PrintJob {
  enum JobType {
    TEXT = 0,
    TWO_COLUMN = 1,
    BARCODE = 2,
    QR_CODE = 3,
    FEED_PAPER = 4,
    SEPARATOR = 5,
    TABLE_ROW = 6,
    ROTATED_TEXT = 7
  };
  
  JobType type;
  std::string data1;        // Primary text/data
  std::string data2;        // Secondary data (right column, etc.)
  std::string data3;        // Tertiary data (third column)
  uint8_t param1;           // Size, type, alignment, etc.
  uint8_t param2;           // Error correction, rotation, etc.
  bool param3;              // Bold, fill dots, header, etc.
  uint32_t timestamp;       // When job was queued
  uint8_t priority;         // Job priority (0=normal, 1=high, 2=emergency)
  
  PrintJob() : type(TEXT), param1(0), param2(0), param3(false), 
               timestamp(0), priority(0) {}
};

class ThermalPrinterComponent : public Component, public uart::UARTDevice, public Print {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Print interface implementation
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Original thermal printer control methods
  void wake();
  void sleep();
  void reset();
  void test();
  void test_page();
  void normal();
  void inverse_on(bool state = true);
  void inverse_off();
  void double_height_on(bool state = true);
  void double_height_off();
  void double_width_on(bool state = true);
  void double_width_off();
  void bold_on(bool state = true);
  void bold_off();
  void underline_on(bool state = true);
  void underline_off();
  void set_size(char value);
  void set_text_size(uint8_t size);
  void set_line_height(uint8_t height = 32);
  void set_bar_code_height(uint8_t height = 50);
  void set_charset(uint8_t charset = 0);
  void set_code_page(uint8_t codePage = 0);
  void feed(uint8_t x = 1);
  void feed_rows(uint8_t rows);
  void print_barcode(const char *text, uint8_t type);
  void print_barcode(int type, const char *text);
  void justify(char value);
  void set_default();
  void offline();
  void online();
  void set_heat_config(uint8_t dots, uint8_t time, uint8_t interval);

  // Enhanced ESPHome specific methods
  void print_text(const char *text);
  void print_two_column(const char *left_text, const char *right_text, bool fill_dots = true, char text_size = 'S');
  void print_table_row(const char *col1, const char *col2, const char *col3 = nullptr);
  bool has_paper();
  void set_paper_check_callback(std::function<void(bool)> &&callback);
  
  // Paper usage estimation
  float get_paper_usage_mm();
  float get_paper_usage_percent(); 
  void reset_paper_usage();
  uint32_t get_lines_printed();
  uint32_t get_characters_printed();
  void set_paper_roll_length(float length_mm); 
  void set_line_height_calibration(float mm_per_line);

  // Enhanced methods with DTR support
  void set_rotation(uint8_t rotation);
  void print_rotated_text(const char* text, uint8_t rotation = 1);
  void print_qr_code(const char* data, uint8_t size = 3, uint8_t error_correction = 1);
  PrintResult safe_print_text(const char* text);
  bool get_detailed_status(PrinterStatus* status);
  bool can_print_job(uint16_t estimated_lines);
  uint16_t estimate_lines_for_text(const char* text);
  float predict_paper_usage_for_job(const char* text, uint8_t text_size);
  void set_heat_config_advanced(uint8_t dots, uint8_t time, uint8_t interval, uint8_t density = 4);
  void print_simple_receipt(const char* business_name, const char* total);
  void print_shopping_list(const char* items_string);
  bool validate_config();
  void print_startup_message();
  void recover_from_error();
  void log_performance_stats();

  // ===== DTR HANDSHAKING METHODS =====
  
  // Configuration methods
  void set_dtr_pin(InternalGPIOPin *pin) { this->dtr_pin_ = pin; }
  void enable_dtr_handshaking(bool enable) { this->dtr_enabled_ = enable; }
  void set_heat_dots(uint8_t dots) { this->heat_dots_ = dots; }
  void set_heat_time(uint8_t time) { this->heat_time_ = time; }
  void set_heat_interval(uint8_t interval) { this->heat_interval_ = interval; }
  
  // DTR handshaking core functions
  void timeout_wait();
  void timeout_set(uint32_t delay_microseconds);
  bool dtr_ready();
  void wait_for_printer_ready(uint32_t timeout_ms = 5000);
  
  // DTR statistics and debugging
  uint32_t get_dtr_timeouts() { return this->dtr_timeout_count_; }
  void reset_dtr_stats() { this->dtr_timeout_count_ = 0; }
  bool is_dtr_enabled() { return this->dtr_enabled_ && this->dtr_pin_ != nullptr; }
  
  // Enhanced write methods with DTR flow control
  void write_byte_with_flow_control(uint8_t byte);
  void write_bytes_with_flow_control(const uint8_t *data, size_t length);

  // ===== NEW: QUEUE MANAGEMENT METHODS =====
  
  // Queue operations
  PrintResult queue_print_job(const PrintJob& job);
  void process_print_queue();
  void clear_print_queue();
  bool is_printer_busy() const { return printer_busy_; }
  uint8_t get_queue_length() const { return print_queue_.size(); }
  
  // Queue configuration
  void set_print_delay(uint32_t delay_ms) { print_delay_ms_ = delay_ms; }
  void set_max_queue_size(uint8_t max_size) { max_queue_size_ = max_size; }
  void enable_auto_queue_processing(bool enable) { auto_process_queue_ = enable; }
  
  // Queue statistics
  uint32_t get_total_jobs_processed() const { return total_jobs_processed_; }
  uint32_t get_jobs_dropped() const { return jobs_dropped_; }
  float get_average_job_time() const;
  
  // Convenience methods for queueing common jobs
  PrintResult queue_text(const char* text, uint8_t size = 2, uint8_t align = 0, bool bold = false, uint8_t priority = 0);
  PrintResult queue_two_column(const char* left, const char* right, bool dots = true, char size = 'S', uint8_t priority = 0);
  PrintResult queue_barcode(uint8_t type, const char* data, uint8_t priority = 0);
  PrintResult queue_qr_code(const char* data, uint8_t size = 3, uint8_t error_correction = 1, uint8_t priority = 0);
  PrintResult queue_separator(uint8_t priority = 0);
  PrintResult queue_feed(uint8_t lines = 1, uint8_t priority = 0);
  
  // Emergency/immediate printing (bypasses queue)
  PrintResult print_immediate(const char* text, uint8_t size = 2, uint8_t align = 1, bool bold = true);

 protected:
  uint32_t last_paper_check_{0};
  bool paper_status_{true};
  optional<std::function<void(bool)>> paper_check_callback_;
  
  // Paper usage tracking
  uint32_t lines_printed_{0};
  uint32_t characters_printed_{0};
  uint32_t feeds_executed_{0};
  float paper_roll_length_{30000.0}; // 30 meters in mm
  float line_height_mm_{4.0}; 

  // Heat configuration storage
  uint8_t heat_dots_{7};
  uint8_t heat_time_{80};
  uint8_t heat_interval_{2};

  // ===== DTR HANDSHAKING VARIABLES =====
  InternalGPIOPin *dtr_pin_{nullptr};
  bool dtr_enabled_{false};
  uint32_t resume_time_micros_{0};
  uint32_t dtr_timeout_count_{0};
  
  // Timing constants for DTR
  uint32_t byte_time_micros_{416};
  uint32_t dot_print_time_micros_{33};
  uint32_t dot_feed_time_micros_{333};
  
  // Performance tracking
  uint32_t total_bytes_sent_{0};
  uint32_t dtr_waits_{0};
  uint32_t timeout_waits_{0};

  // ===== NEW: QUEUE SYSTEM VARIABLES =====
  std::queue<PrintJob> print_queue_;
  bool printer_busy_{false};
  bool auto_process_queue_{true};
  uint32_t last_print_time_{0};
  uint32_t print_delay_ms_{2000};        // 2 second delay between jobs
  uint8_t max_queue_size_{10};           // Maximum jobs in queue
  
  // Queue statistics
  uint32_t total_jobs_processed_{0};
  uint32_t jobs_dropped_{0};
  uint32_t total_processing_time_{0};
  uint32_t current_job_start_time_{0};

  // Helper methods for paper tracking
  void track_print_operation(uint16_t chars, uint8_t lines = 0, uint8_t feeds = 0);
  void save_usage_to_flash();
  void load_usage_from_flash();
  
  // Two column formatting helpers
  void print_padded_line(const char *left, const char *right, uint8_t total_width, char pad_char = '.');
  
  // Helper methods
  void write_bytes(uint8_t a);
  void write_bytes(uint8_t a, uint8_t b);
  void write_bytes(uint8_t a, uint8_t b, uint8_t c);
  void write_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
  
  // ===== NEW: QUEUE HELPER METHODS =====
  void execute_print_job(const PrintJob& job);
  bool should_process_queue();
  void update_queue_statistics(uint32_t job_duration);
  PrintJob create_text_job(const char* text, uint8_t size, uint8_t align, bool bold, uint8_t priority);
  
  // DTR helper methods
  void initialize_dtr_timings();
  void update_performance_stats();
  uint32_t calculate_operation_time_micros(uint8_t operation_type, uint8_t data_length = 1);
};

}  // namespace thermal_printer
}  // namespace esphome
