#include "thermal_printer.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

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

void ThermalPrinterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Thermal Printer...");
  
  // Wait for printer to initialize
  delay(500);
  
  // Wake up and initialize printer
  this->wake();
  
  // Set heat config
  this->set_heat_config(this->heat_dots_, this->heat_time_, this->heat_interval_);
  
  // Set defaults
  this->set_default();
  
  ESP_LOGCONFIG(TAG, "Thermal Printer setup complete");
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
  LOG_PIN("  TX Pin: ", this->tx_pin_);
  LOG_PIN("  RX Pin: ", this->rx_pin_);
  ESP_LOGCONFIG(TAG, "  Baud Rate: %d", this->parent_->get_baud_rate());
}

// Print interface implementation
size_t ThermalPrinterComponent::write(uint8_t c) {
  if (this->is_sleeping_) {
    this->wake();
  }
  
  this->write_byte(c);
  
  uint8_t d = c;
  if ((d != 0x13) && (d != 0x11)) {  // Strip DC1/DC3 flow control
    this->column_ = ((d == '\n') || (this->column_ == this->max_column_)) ? 0 : (this->column_ + 1);
    this->prev_byte_ = d;
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
  this->write_bytes(255);           // Wake
  if (this->is_sleeping_) {
    this->is_sleeping_ = false;
    delay(50);
    this->set_heat_config(this->heat_dots_, this->heat_time_, this->heat_interval_);
  }
}

void ThermalPrinterComponent::sleep() {
  this->write_bytes(ASCII_ESC, '8', 0, 0);  // Sleep
  this->is_sleeping_ = true;
}

void ThermalPrinterComponent::reset() {
  this->write_bytes(ASCII_ESC, '@');
  this->prev_byte_ = '\n';
  this->column_ = 0;
  this->max_column_ = 32;
  this->char_height_ = 24;
  this->line_spacing_ = 8;
  this->bar_code_height_ = 50;
  this->print_mode_ = 0;
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
}

void ThermalPrinterComponent::set_heat_config(uint8_t dots, uint8_t time, uint8_t interval) {
  this->write_bytes(ASCII_ESC, '7');   // Esc 7 (print settings)
  this->write_bytes(dots);             // Heating dots (20=balance of darkness vs no jams)
  this->write_bytes(time);             // Library default = 80 (heat time)
  this->write_bytes(interval);         // Library default = 2 (heat interval)
  
  // Modify tab stops
  this->write_bytes(ASCII_ESC, 'D');   // Set tab stops...
  this->write_bytes(4, 8, 12, 16);     // ...every 4 columns,
  this->write_bytes(20, 24, 28, 0);    // 0 marks end of list.
}

void ThermalPrinterComponent::normal() {
  this->print_mode_ = 0;
  this->write_print_mode();
}

void ThermalPrinterComponent::inverse_on(bool state) {
  if (state) {
    this->print_mode_ |= INVERSE;
  } else {
    this->print_mode_ &= ~INVERSE;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::inverse_off() {
  this->inverse_on(false);
}

void ThermalPrinterComponent::upside_down_on(bool state) {
  if (state) {
    this->print_mode_ |= UPDOWN;
  } else {
    this->print_mode_ &= ~UPDOWN;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::upside_down_off() {
  this->upside_down_on(false);
}

void ThermalPrinterComponent::double_height_on(bool state) {
  if (state) {
    this->print_mode_ |= DOUBLE_HEIGHT;
  } else {
    this->print_mode_ &= ~DOUBLE_HEIGHT;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::double_height_off() {
  this->double_height_on(false);
}

void ThermalPrinterComponent::double_width_on(bool state) {
  if (state) {
    this->print_mode_ |= DOUBLE_WIDTH;
  } else {
    this->print_mode_ &= ~DOUBLE_WIDTH;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::double_width_off() {
  this->double_width_on(false);
}

void ThermalPrinterComponent::strike_on(bool state) {
  if (state) {
    this->print_mode_ |= STRIKE;
  } else {
    this->print_mode_ &= ~STRIKE;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::strike_off() {
  this->strike_on(false);
}

void ThermalPrinterComponent::bold_on(bool state) {
  if (state) {
    this->print_mode_ |= BOLD;
  } else {
    this->print_mode_ &= ~BOLD;
  }
  this->write_print_mode();
}

void ThermalPrinterComponent::bold_off() {
  this->bold_on(false);
}

void ThermalPrinterComponent::underline_on(bool state) {
  if (state) {
    this->write_bytes(ASCII_ESC, '-', 1);
  } else {
    this->write_bytes(ASCII_ESC, '-', 0);
  }
}

void ThermalPrinterComponent::underline_off() {
  this->underline_on(false);
}

void ThermalPrinterComponent::set_size(char value) {
  int s = value;
  if (s == 'L') {  // Large: double width and height
    this->set_text_size(3);
    this->char_height_ = 48;
    this->max_column_ = 16;
  } else if (s == 'M') {  // Medium: double height
    this->set_text_size(2);
    this->char_height_ = 48;
    this->max_column_ = 32;
  } else {  // Small: standard width and height
    this->set_text_size(1);
    this->char_height_ = 24;
    this->max_column_ = 32;
  }
  this->prev_byte_ = '\n';
  this->column_ = 0;
}

void ThermalPrinterComponent::set_text_size(uint8_t size) {
  uint8_t s = size - 1;
  if (s > 7) s = 7;  // Limit to 0-7
  
  this->write_bytes(ASCII_GS, '!', s);
  
  // Update internal state
  if (size >= 3) {
    this->char_height_ = 48;
    this->max_column_ = 16;
  } else if (size == 2) {
    this->char_height_ = 48;
    this->max_column_ = 32;
  } else {
    this->char_height_ = 24;
    this->max_column_ = 32;
  }
}

void ThermalPrinterComponent::set_line_height(uint8_t height) {
  if (height < 24) height = 24;
  this->line_spacing_ = height - this->char_height_;
  this->write_bytes(ASCII_ESC, '3', this->line_spacing_);
}

void ThermalPrinterComponent::set_bar_code_height(uint8_t height) {
  if (height < 1) height = 1;
  this->bar_code_height_ = height;
  this->write_bytes(ASCII_GS, 'h', height);
}

void ThermalPrinterComponent::set_charset(uint8_t charset) {
  this->write_bytes(ASCII_ESC, 'R', charset);
}

void ThermalPrinterComponent::set_code_page(uint8_t codePage) {
  this->write_bytes(ASCII_ESC, 't', codePage);
}

void ThermalPrinterComponent::tab() {
  this->write(ASCII_TAB);
}

void ThermalPrinterComponent::set_char_spacing(uint8_t spacing) {
  this->write_bytes(ASCII_ESC, ' ', spacing);
}

void ThermalPrinterComponent::feed(uint8_t x) {
  this->write_bytes(ASCII_ESC, 'd', x);
}

void ThermalPrinterComponent::feed_rows(uint8_t rows) {
  this->write_bytes(ASCII_ESC, 'J', rows);
}

void ThermalPrinterComponent::flush() {
  this->write_bytes(ASCII_FF);
}

void ThermalPrinterComponent::print_barcode(const char *text, uint8_t type) {
  this->write_bytes(ASCII_GS, 'H', 2);     // Print label below barcode
  this->write_bytes(ASCII_GS, 'w', 3);     // Barcode width
  this->write_bytes(ASCII_GS, 'k', type);  // Barcode type
  
  // Print the barcode data
  for (const char *p = text; *p != 0; p++) {
    this->write(*p);
  }
  
  this->write_bytes(0);  // Null terminate
  delay(300);
}

void ThermalPrinterComponent::print_barcode(int type, const char *text) {
  this->print_barcode(text, (uint8_t)type);
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

void ThermalPrinterComponent::offline() {
  this->write_bytes(ASCII_ESC, '=', 0);
}

void ThermalPrinterComponent::online() {
  this->write_bytes(ASCII_ESC, '=', 1);
}

void ThermalPrinterComponent::beep() {
  this->write_bytes(ASCII_ESC, 'B', 3, 3);
}

// ESPHome specific methods
void ThermalPrinterComponent::print_text(const char *text) {
  this->print(text);
}

bool ThermalPrinterComponent::has_paper() {
  // Send paper status command
  this->write_bytes(ASCII_ESC, 'v', 0);
  
  // Wait for response
  delay(100);
  
  // Read response if available
  if (this->available()) {
    uint8_t status = this->read();
    // Bit 2 and 3 indicate paper status
    return (status & 0x0C) == 0;  // Paper present if bits 2,3 are 0
  }
  
  // Default to paper present if no response
  return true;
}

void ThermalPrinterComponent::set_paper_check_callback(std::function<void(bool)> &&callback) {
  this->paper_check_callback_ = callback;
}

// Helper methods
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

void ThermalPrinterComponent::write_print_mode() {
  this->write_bytes(ASCII_ESC, '!', this->print_mode_);
}

}  // namespace thermal_printer
}  // namespace esphome
