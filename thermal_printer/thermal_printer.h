#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"

// Forward declarations for compatibility
class Stream;
class Print;

namespace esphome {
namespace thermal_printer {

static const char *const TAG = "thermal_printer";

// Barcode types for compatibility
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

  // Thermal printer methods
  void wake();
  void sleep();
  void reset();
  void test();
  void test_page();
  void normal();
  void inverse_on(bool state = true);
  void inverse_off();
  void upside_down_on(bool state = true);
  void upside_down_off();
  void double_height_on(bool state = true);
  void double_height_off();
  void double_width_on(bool state = true);
  void double_width_off();
  void strike_on(bool state = true);
  void strike_off();
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
  void tab();
  void set_char_spacing(uint8_t spacing);
  void set_times(unsigned long x, unsigned long y);
  void feed(uint8_t x = 1);
  void feed_rows(uint8_t rows);
  void flush();
  void print_bitmap(int w, int h, const uint8_t *bitmap, bool fromProgMem = true);
  void print_bitmap(int w, int h, Stream *stream);
  void print_barcode(const char *text, uint8_t type);
  void print_barcode(int type, const char *text);
  void justify(char value);
  void set_default();
  void offline();
  void online();
  void tab_stops(const uint8_t *stops);
  void beep();
  void set_heat_config(uint8_t dots, uint8_t time, uint8_t interval);

  // ESPHome specific methods
  void print_text(const char *text);
  bool has_paper();
  void set_paper_check_callback(std::function<void(bool)> &&callback);

 protected:
  void write_bytes(uint8_t a);
  void write_bytes(uint8_t a, uint8_t b);
  void write_bytes(uint8_t a, uint8_t b, uint8_t c);
  void write_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

  uint32_t last_paper_check_{0};
  bool paper_status_{true};
  optional<std::function<void(bool)>> paper_check_callback_;
  
  // Printer state
  bool is_sleeping_{false};
  uint8_t prev_byte_{'\n'};
  uint8_t column_{0};
  uint8_t max_column_{32};
  uint8_t char_height_{24};
  uint8_t line_spacing_{8};
  uint8_t bar_code_height_{50};
  
  // Heat settings
  uint8_t heat_dots_{7};      // 11*30us = 330us
  uint8_t heat_time_{80};     // 80*10us = 800us  
  uint8_t heat_interval_{2};  // 2*10us = 20us

  // Print modes
  uint8_t print_mode_{0};
  static const uint8_t INVERSE = 1 << 1;
  static const uint8_t UPDOWN = 1 << 2;
  static const uint8_t BOLD = 1 << 3;
  static const uint8_t DOUBLE_HEIGHT = 1 << 4;
  static const uint8_t DOUBLE_WIDTH = 1 << 5;
  static const uint8_t STRIKE = 1 << 6;
};

}  // namespace thermal_printer
}  // namespace esphome
