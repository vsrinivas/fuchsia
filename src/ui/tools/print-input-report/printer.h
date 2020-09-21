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

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

static_assert(static_cast<int>(fuchsia_input_report::UnitType::NONE) == 0);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::OTHER) == 1);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::METERS) == 2);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::GRAMS) == 3);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::DEGREES) == 4);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::ENGLISH_ANGULAR_VELOCITY) == 5);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::SI_LINEAR_VELOCITY) == 6);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::SI_LINEAR_ACCELERATION) == 7);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::WEBERS) == 8);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::CANDELAS) == 9);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::PASCALS) == 10);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::LUX) == 11);
static_assert(static_cast<int>(fuchsia_input_report::UnitType::SECONDS) == 12);

// These strings must be ordered based on the enums in fuchsia.input.report/units.fidl.
const char* const kUnitStrings[] = {
    "NONE",
    "OTHER",
    "METERS",
    "GRAMS",
    "DEGREES",
    "ENGLISH_ANGULAR_VELOCITY",
    "SI_LINEAR_VELOCITY",
    "SI_ACCELERATION",
    "WEBERS",
    "CANDELAS",
    "PASCALS",
    "LUX",
    "SECONDS",
};

static_assert(static_cast<int>(fuchsia_input_report::SensorType::ACCELEROMETER_X) == 1);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::ACCELEROMETER_Y) == 2);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::ACCELEROMETER_Z) == 3);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::MAGNETOMETER_X) == 4);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::MAGNETOMETER_Y) == 5);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::MAGNETOMETER_Z) == 6);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::GYROSCOPE_X) == 7);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::GYROSCOPE_Y) == 8);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::GYROSCOPE_Z) == 9);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE) == 10);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::LIGHT_RED) == 11);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::LIGHT_GREEN) == 12);
static_assert(static_cast<int>(fuchsia_input_report::SensorType::LIGHT_BLUE) == 13);

// These strings must be ordered based on the enums in fuchsia.input.report/sensor.fidl.
const char* const kSensorTypeStrings[] = {
    "ERROR",          "ACCELEROMETER_X", "ACCELEROMETER_Y",   "ACCELEROMETER_Z",
    "MAGNETOMETER_X", "MAGNETOMETER_Y",  "MAGNETOMETER_Z",    "GYROSCOPE_X",
    "GYROSCOPE_Y",    "GYROSCOPE_Z",     "LIGHT_ILLUMINANCE", "LIGHT_RED",
    "LIGHT_GREEN",    "LIGHT_BLUE",
};

static_assert(static_cast<int>(fuchsia_input_report::TouchType::TOUCHSCREEN) == 1);
// These strings must be ordered based on the enums in fuchsia.input.report/touch.fidl.
const char* const kTouchTypeStrings[] = {
    "ERROR",
    "TOUCHSCREEN",
};

static_assert(static_cast<int>(fuchsia_input_report::LedType::NUM_LOCK) == 1);
static_assert(static_cast<int>(fuchsia_input_report::LedType::CAPS_LOCK) == 2);
static_assert(static_cast<int>(fuchsia_input_report::LedType::SCROLL_LOCK) == 3);
static_assert(static_cast<int>(fuchsia_input_report::LedType::COMPOSE) == 4);
static_assert(static_cast<int>(fuchsia_input_report::LedType::KANA) == 5);
// These strings must be ordered based on the enums in fuchsia.input.report/led.fidl.
const char* const kLedTypeStrings[] = {
    "ERROR", "NUM_LOCK", "CAPS_LOCK", "SCROLL_LOCK", "COMPOSE", "KANA",
};

static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::VOLUME_UP) == 1);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::VOLUME_DOWN) == 2);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::PAUSE) == 3);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::FACTORY_RESET) == 4);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::MIC_MUTE) == 5);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::REBOOT) == 6);
static_assert(static_cast<int>(fuchsia_input_report::ConsumerControlButton::CAMERA_DISABLE) == 7);
// These strings must be ordered based on the enums in fuchsia.input.report/consumer_control.fidl.
const char* const kConsumerControlButtonStrings[] = {
    "ERROR",         "VOLUME_UP", "VOLUME_DOWN", "PAUSE",
    "FACTORY_RESET", "MIC_MUTE",  "REBOOT",      "CAMERA_DISABLE",
};

class Printer {
 public:
  Printer() = default;

  // Find the string related to the unit. If we are given a value that we do not
  // recognize, the string "NONE" will be returned and printed.
  static const char* UnitToString(fuchsia_input_report::Unit unit) {
    uint32_t unit_index = static_cast<uint32_t>(unit.type);
    if (unit_index >= countof(kUnitStrings)) {
      return kUnitStrings[0];
    }
    return kUnitStrings[unit_index];
  }

  // Find the string related to the sensor type. If we are given a value that we do not
  // recognize, the string "ERROR" will be returned and printed.
  static const char* SensorTypeToString(fuchsia_input_report::SensorType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= countof(kSensorTypeStrings)) {
      return kSensorTypeStrings[0];
    }
    return kSensorTypeStrings[unit_index];
  }

  static const char* TouchTypeToString(fuchsia_input_report::TouchType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= countof(kTouchTypeStrings)) {
      return kTouchTypeStrings[0];
    }
    return kTouchTypeStrings[unit_index];
  }

  static const char* LedTypeToString(fuchsia_input_report::LedType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= countof(kLedTypeStrings)) {
      return kLedTypeStrings[0];
    }
    return kLedTypeStrings[unit_index];
  }

  static const char* ConsumerControlButtonToString(
      fuchsia_input_report::ConsumerControlButton type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= countof(kConsumerControlButtonStrings)) {
      return kConsumerControlButtonStrings[0];
    }
    return kConsumerControlButtonStrings[unit_index];
  }

  void PrintAxis(fuchsia_input_report::Axis axis) {
    this->Print("Unit: %8s\n", UnitToString(axis.unit));
    this->Print("Min:  %8ld\n", axis.range.min);
    this->Print("Max:  %8ld\n", axis.range.max);
  }

  void PrintAxisIndented(fuchsia_input_report::Axis axis) {
    IncreaseIndent();
    this->Print("Unit: %8s\n", UnitToString(axis.unit));
    this->Print("Min:  %8ld\n", axis.range.min);
    this->Print("Max:  %8ld\n", axis.range.max);
    DecreaseIndent();
  }

  void Print(const char* format, ...) {
    std::string str_format(indent_, ' ');
    str_format += format;

    va_list argptr;
    va_start(argptr, format);
    RealPrint(str_format.c_str(), argptr);
    va_end(argptr);
  }

  void SetIndent(size_t indent) { indent_ = indent; }

  void IncreaseIndent() { indent_ += 2; }

  void DecreaseIndent() { indent_ -= 2; }

 protected:
  virtual void RealPrint(const char* format, va_list argptr) { vprintf(format, argptr); }
  size_t indent_ = 0;
};

}  // namespace print_input_report

#endif  // SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_
