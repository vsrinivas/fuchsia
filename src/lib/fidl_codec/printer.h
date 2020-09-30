// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_PRINTER_H_
#define SRC_LIB_FIDL_CODEC_PRINTER_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/rights.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <cinttypes>
#include <cstdint>
#include <ostream>

namespace fidl_codec {

constexpr int kTabSize = 2;
constexpr uint64_t kOneBillion = 1'000'000'000L;

#define kSecondsPerMinute 60
#define kMinutesPerHour 60
#define kHoursPerDay 24

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
  PrettyPrinter(std::ostream& os, const Colors& colors, bool pretty_print,
                std::string_view line_header, int max_line_size, bool header_on_every_line,
                int tabulations = 0);

  std::ostream& os() const { return os_; }
  const Colors& colors() const { return colors_; }
  bool pretty_print() const { return pretty_print_; }
  int max_line_size() const { return max_line_size_; }
  bool header_on_every_line() const { return header_on_every_line_; }
  void set_header_on_every_line(bool header_on_every_line) {
    header_on_every_line_ = header_on_every_line;
  }
  int remaining_size() const { return remaining_size_; }

  bool LineEmpty() const { return need_to_print_header_; }

  virtual bool DumpMessages() const { return false; }

  // Displays a handle. This allows the caller to also display some infered data we have inferered
  // for this handle (if any).
  virtual void DisplayHandle(const zx_handle_info_t& handle);

  // Displays a bti perm.
  void DisplayBtiPerm(uint32_t perm);

  // Displays a cache policy.
  void DisplayCachePolicy(uint32_t cache_policy);

  // Displays a clock.
  void DisplayClock(zx_clock_t clock);

  // Displays a duration.
  void DisplayDuration(zx_duration_t duration_ns);

  // Displays an exception state.
  void DisplayExceptionState(uint32_t state);

  // Displays a gpaddr.
  void DisplayGpAddr(zx_gpaddr_t addr);

  // Displays a uint8_t value in padded hexadecimal format.
  void DisplayHexa8(uint8_t value);

  // Displays a uint16_t value in padded hexadecimal format.
  void DisplayHexa16(uint16_t value);

  // Displays a uint32_t value in padded hexadecimal format.
  void DisplayHexa32(uint32_t value);

  // Displays a uint64_t value in padded hexadecimal format.
  void DisplayHexa64(uint64_t value);

  // Displays an object info topic.
  void DisplayObjectInfoTopic(uint32_t topic);

  // Displays a packet guest_vcpu type.
  void DisplayPacketGuestVcpuType(uint8_t type);

  // Displays a packet page request command.
  void DisplayPacketPageRequestCommand(uint16_t command);

  // Displays a paddr.
  void DisplayPaddr(zx_paddr_t addr);

  // Displays PCI bar type.
  void DisplayPciBarType(uint32_t type);

  // Displays profile info flags name.
  void DisplayProfileInfoFlags(uint32_t flags);

  // Displays prop type.
  void DisplayPropType(uint32_t type);

  // Displays port packet type.
  void DisplayPortPacketType(uint32_t type);

  // Displays rights.
  void DisplayRights(uint32_t rights);

  // Displays signals
  void DisplaySignals(zx_signals_t signals);

  // Displays status.
  void DisplayStatus(zx_status_t status);

  // Displays string.
  void DisplayString(std::string_view string);

  // Displays a time.
  void DisplayTime(zx_time_t time_ns);

  // Displays a uintptr.
#ifdef __MACH__
  void DisplayUintptr(uintptr_t ptr);
#else
  void DisplayUintptr(uint64_t ptr);
#endif

  // Displays a vaddr.
  void DisplayVaddr(zx_vaddr_t addr);

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

  PrettyPrinter& operator<<(int32_t data) {
    FX_DCHECK((os_.flags() & os_.basefield) == os_.dec);
    *this << std::to_string(data);
    return *this;
  }

  PrettyPrinter& operator<<(int64_t data) {
    FX_DCHECK((os_.flags() & os_.basefield) == os_.dec);
    *this << std::to_string(data);
    return *this;
  }

  PrettyPrinter& operator<<(uint32_t data) {
    if (((os_.flags() & os_.basefield) == os_.dec) || (data == 0)) {
      *this << std::to_string(data);
    } else {
      os_ << data;
      int data_size = 0;
      while (data > 0) {
        data /= 16;
        data_size++;
      }
      remaining_size_ -= data_size;
    }
    return *this;
  }

  PrettyPrinter& operator<<(uint64_t data) {
    if (((os_.flags() & os_.basefield) == os_.dec) || (data == 0)) {
      *this << std::to_string(data);
    } else {
      os_ << data;
      int data_size = 0;
      while (data > 0) {
        data /= 16;
        data_size++;
      }
      remaining_size_ -= data_size;
    }
    return *this;
  }

#ifdef __MACH__
  // On MacOs, the type of size_t is unsigned long.
  PrettyPrinter& operator<<(size_t data) {
    FX_DCHECK((os_.flags() & os_.basefield) == os_.dec);
    *this << std::to_string(data);
    return *this;
  }
#endif

  PrettyPrinter& operator<<(std::string_view data);

  // Used by the color functions.
  PrettyPrinter& operator<<(PrettyPrinter& (*pf)(PrettyPrinter& printer)) {
    if (need_to_print_header_) {
      PrintHeader(' ');
    }
    return pf(*this);
  }

  // Used by std::hex, std::dec.
  PrettyPrinter& operator<<(std::ios_base& (*pf)(std::ios_base&)) {
    pf(os_);
    return *this;
  }

 private:
  std::ostream& os_;
  const Colors& colors_;
  const bool pretty_print_;
  const std::string_view line_header_;
  const int max_line_size_;
  bool header_on_every_line_;
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

// Scope which increments the indentation several times.
class MultiIndent {
 public:
  explicit MultiIndent(PrettyPrinter& printer, int count) : printer_(printer), count_(count) {
    while (count > 0) {
      printer.IncrementTabulations();
      --count;
    }
  }
  ~MultiIndent() {
    while (count_ > 0) {
      printer_.DecrementTabulations();
      --count_;
    }
  }

 private:
  PrettyPrinter& printer_;
  int count_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_PRINTER_H_
