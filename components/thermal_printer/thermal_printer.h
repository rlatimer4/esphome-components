#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer";

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

class ThermalPrinterComponent : public Component, public uart::UARTDevice, public Print {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Print interface implementation
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Thermal printer control methods
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

  // ESPHome specific methods
  void print_text(const char *text);
  void print_two_column(const char *left_text, const char *right_text, bool fill_dots = true);
  void print_table_row(const char *col1, const char *col2, const char *col3 = nullptr);
  bool has_paper();
  void set_paper_check_callback(std::function<void(bool)> &&callback);
  
  // Paper usage estimation
  float get_paper_usage_mm();
  float get_paper_usage_percent(); // Based on 30m roll
  void reset_paper_usage();
  uint32_t get_lines_printed();
  uint32_t get_characters_printed();
  void set_paper_roll_length(float length_mm); // Default 30000mm (30m)

 protected:
  uint32_t last_paper_check_{0};
  bool paper_status_{true};
  optional<std::function<void(bool)>> paper_check_callback_;
  
  // Paper usage tracking
  uint32_t lines_printed_{0};
  uint32_t characters_printed_{0};
  uint32_t feeds_executed_{0};
  float paper_roll_length_{30000.0}; // 30 meters in mm
  float line_height_mm_{0.125}; // ~0.125mm per line for thermal paper

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
};

}  // namespace thermal_printer
}  // namespace esphome
