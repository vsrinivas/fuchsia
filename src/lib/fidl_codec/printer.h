// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_PRINTER_H_
#define SRC_LIB_FIDL_CODEC_PRINTER_H_

#include <zircon/system/public/zircon/rights.h>
#include <zircon/system/public/zircon/syscalls/exception.h>
#include <zircon/system/public/zircon/syscalls/object.h>
#include <zircon/system/public/zircon/syscalls/resource.h>
#include <zircon/system/public/zircon/types.h>

#include <cinttypes>
#include <cstdint>
#include <ostream>

#include "zircon/system/public/zircon/syscalls/debug.h"

namespace fidl_codec {

constexpr int kTabSize = 2;

struct Colors {
  Colors(const char* new_reset, const char* new_red, const char* new_green, const char* new_blue,
         const char* new_white_on_magenta, const char* new_yellow_background)
      : reset(new_reset),
        red(new_red),
        green(new_green),
        blue(new_blue),
        white_on_magenta(new_white_on_magenta),
        yellow_background(new_yellow_background) {}

  const char* const reset;
  const char* const red;
  const char* const green;
  const char* const blue;
  const char* const white_on_magenta;
  const char* const yellow_background;
};

extern const Colors WithoutColors;
extern const Colors WithColors;

class PrettyPrinter {
 public:
  PrettyPrinter(std::ostream& os, const Colors& colors, std::string_view line_header,
                int max_line_size, bool header_on_every_line, int tabulations = 0);

  std::ostream& os() const { return os_; }
  const Colors& colors() const { return colors_; }
  int max_line_size() const { return max_line_size_; }
  int remaining_size() const { return remaining_size_; }

  bool LineEmpty() const { return need_to_print_header_; }

  // Displays a handle. This allows the caller to also display some infered data we have inferered
  // for this handle (if any).
  virtual void DisplayHandle(const zx_handle_info_t& handle);

  void IncrementTabulations();
  void DecrementTabulations();
  void NeedHeader();
  void PrintHeader(char first_character);

  PrettyPrinter& operator<<(char data) {
    if (need_to_print_header_) {
      PrintHeader(data);
    }
    os_ << data;
    if (data == '\n') {
      NeedHeader();
    } else {
      --remaining_size_;
    }
    return *this;
  }

  PrettyPrinter& operator<<(std::string_view data);

  // Used by the color functions.
  PrettyPrinter& operator<<(PrettyPrinter& (*pf)(PrettyPrinter& printer)) {
    if (need_to_print_header_) {
      PrintHeader(' ');
    }
    return pf(*this);
  }

 private:
  std::ostream& os_;
  const Colors& colors_;
  const std::string_view line_header_;
  const int max_line_size_;
  const bool header_on_every_line_ = false;
  bool need_to_print_header_ = true;
  int line_header_size_ = 0;
  int tabulations_;
  int remaining_size_ = 0;
};

inline PrettyPrinter& ResetColor(PrettyPrinter& printer) {
  printer.os() << printer.colors().reset;
  return printer;
}

inline PrettyPrinter& Red(PrettyPrinter& printer) {
  printer.os() << printer.colors().red;
  return printer;
}

inline PrettyPrinter& Green(PrettyPrinter& printer) {
  printer.os() << printer.colors().green;
  return printer;
}

inline PrettyPrinter& Blue(PrettyPrinter& printer) {
  printer.os() << printer.colors().blue;
  return printer;
}

inline PrettyPrinter& WhiteOnMagenta(PrettyPrinter& printer) {
  printer.os() << printer.colors().white_on_magenta;
  return printer;
}

inline PrettyPrinter& YellowBackground(PrettyPrinter& printer) {
  printer.os() << printer.colors().yellow_background;
  return printer;
}

// Scope which increments the indentation.
class Indent {
 public:
  explicit Indent(PrettyPrinter& printer) : printer_(printer) { printer.IncrementTabulations(); }
  ~Indent() { printer_.DecrementTabulations(); }

 private:
  PrettyPrinter& printer_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_PRINTER_H_
