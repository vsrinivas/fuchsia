// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_
#define SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <stdarg.h>
#include <stdio.h>

#include <string>

namespace print_input_report {

static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::NONE) == 0);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::OTHER) == 1);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::DISTANCE) == 2);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::WEIGHT) == 3);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::ROTATION) == 4);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::ANGULAR_VELOCITY) == 5);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::LINEAR_VELOCITY) == 6);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::ACCELERATION) == 7);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::MAGNETIC_FLUX) == 8);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::LUMINOUS_FLUX) == 9);
static_assert(static_cast<int>(::llcpp::fuchsia::input::report::Unit::PRESSURE) == 10);

// These strings must be ordered based on the enums in fuchsia.input.report/units.fidl.
const char* const kUnitStrings[] = {
    "NONE",
    "OTHER",
    "DISTANCE",
    "WEIGHT",
    "ROTATION",
    "ANGULAR_VELOCITY",
    "LINEAR_VELOCITY",
    "ACCELERATION",
    "MAGNETIC_FLUX",
    "LUMINOUS_FLUX",
    "PRESSURE",
};

class Printer {
 public:
  Printer() = default;

  static const char* UnitToString(::llcpp::fuchsia::input::report::Unit unit) {
    uint32_t unit_index = static_cast<uint32_t>(unit);
    if (unit_index >= countof(kUnitStrings)) {
      return kUnitStrings[0];
    }
    return kUnitStrings[unit_index];
  }

  void PrintAxis(::llcpp::fuchsia::input::report::Axis axis) const {
    this->Print("Unit: %8s\n", UnitToString(axis.unit));
    this->Print("Min:  %8ld\n", axis.range.min);
    this->Print("Max:  %8ld\n", axis.range.max);
  }

  void PrintAxisIndented(::llcpp::fuchsia::input::report::Axis axis) {
    IncreaseIndent();
    this->Print("Unit: %8s\n", UnitToString(axis.unit));
    this->Print("Min:  %8ld\n", axis.range.min);
    this->Print("Max:  %8ld\n", axis.range.max);
    DecreaseIndent();
  }

  void Print(const char* format, ...) const {
    std::string spaces(indent_, ' ');
    printf("%s", spaces.c_str());

    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    va_end(argptr);
  }

  void SetIndent(size_t indent) { indent_ = indent; }

  void IncreaseIndent() { indent_ += 2; }

  void DecreaseIndent() { indent_ -= 2; }

 private:
  size_t indent_ = 0;
};

}  // namespace print_input_report

#endif  // SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_
