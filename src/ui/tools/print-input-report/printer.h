// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_
#define SRC_UI_TOOLS_PRINT_INPUT_REPORT_PRINTER_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <stdarg.h>
#include <stdio.h>

#include <string>

namespace print_input_report {

static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kNone) == 0);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kOther) == 1);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kMeters) == 2);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kGrams) == 3);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kDegrees) == 4);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kEnglishAngularVelocity) ==
              5);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kSiLinearVelocity) == 6);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kSiLinearAcceleration) == 7);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kWebers) == 8);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kCandelas) == 9);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kPascals) == 10);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kLux) == 11);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::UnitType::kSeconds) == 12);

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

static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kAccelerometerX) == 1);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kAccelerometerY) == 2);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kAccelerometerZ) == 3);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kMagnetometerX) == 4);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kMagnetometerY) == 5);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kMagnetometerZ) == 6);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kGyroscopeX) == 7);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kGyroscopeY) == 8);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kGyroscopeZ) == 9);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kLightIlluminance) == 10);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kLightRed) == 11);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kLightGreen) == 12);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::SensorType::kLightBlue) == 13);

// These strings must be ordered based on the enums in fuchsia.input.report/sensor.fidl.
const char* const kSensorTypeStrings[] = {
    "ERROR",          "ACCELEROMETER_X", "ACCELEROMETER_Y",   "ACCELEROMETER_Z",
    "MAGNETOMETER_X", "MAGNETOMETER_Y",  "MAGNETOMETER_Z",    "GYROSCOPE_X",
    "GYROSCOPE_Y",    "GYROSCOPE_Z",     "LIGHT_ILLUMINANCE", "LIGHT_RED",
    "LIGHT_GREEN",    "LIGHT_BLUE",
};

static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::TouchType::kTouchscreen) == 1);
// These strings must be ordered based on the enums in fuchsia.input.report/touch.fidl.
const char* const kTouchTypeStrings[] = {
    "ERROR",
    "TOUCHSCREEN",
};

static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::LedType::kNumLock) == 1);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::LedType::kCapsLock) == 2);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::LedType::kScrollLock) == 3);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::LedType::kCompose) == 4);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::LedType::kKana) == 5);
// These strings must be ordered based on the enums in fuchsia.input.report/led.fidl.
const char* const kLedTypeStrings[] = {
    "ERROR", "NUM_LOCK", "CAPS_LOCK", "SCROLL_LOCK", "COMPOSE", "KANA",
};

static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kVolumeUp) ==
              1);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kVolumeDown) ==
              2);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kPause) == 3);
static_assert(
    fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kFactoryReset) == 4);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kMicMute) == 5);
static_assert(fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kReboot) == 6);
static_assert(
    fidl::ToUnderlying(fuchsia_input_report::wire::ConsumerControlButton::kCameraDisable) == 7);
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
  static const char* UnitTypeToString(fuchsia_input_report::wire::Unit unit) {
    uint32_t unit_index = static_cast<uint32_t>(unit.type);
    if (unit_index >= std::size(kUnitStrings)) {
      return kUnitStrings[0];
    }
    return kUnitStrings[unit_index];
  }

  // Find the string related to the sensor type. If we are given a value that we do not
  // recognize, the string "ERROR" will be returned and printed.
  static const char* SensorTypeToString(fuchsia_input_report::wire::SensorType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= std::size(kSensorTypeStrings)) {
      return kSensorTypeStrings[0];
    }
    return kSensorTypeStrings[unit_index];
  }

  static const char* TouchTypeToString(fuchsia_input_report::wire::TouchType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= std::size(kTouchTypeStrings)) {
      return kTouchTypeStrings[0];
    }
    return kTouchTypeStrings[unit_index];
  }

  static const char* LedTypeToString(fuchsia_input_report::wire::LedType type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= std::size(kLedTypeStrings)) {
      return kLedTypeStrings[0];
    }
    return kLedTypeStrings[unit_index];
  }

  static const char* ConsumerControlButtonToString(
      fuchsia_input_report::wire::ConsumerControlButton type) {
    uint32_t unit_index = static_cast<uint32_t>(type);
    if (unit_index >= std::size(kConsumerControlButtonStrings)) {
      return kConsumerControlButtonStrings[0];
    }
    return kConsumerControlButtonStrings[unit_index];
  }

  void PrintAxis(fuchsia_input_report::wire::Axis axis) {
    if (axis.unit.exponent) {
      this->Print("Unit: %8s * 1e%d\n", UnitTypeToString(axis.unit), axis.unit.exponent);
    } else {
      this->Print("Unit: %8s\n", UnitTypeToString(axis.unit));
    }
    this->Print("Min:  %8ld\n", axis.range.min);
    this->Print("Max:  %8ld\n", axis.range.max);
  }

  void PrintAxisIndented(fuchsia_input_report::wire::Axis axis) {
    IncreaseIndent();
    if (axis.unit.exponent) {
      this->Print("Unit: %8s * 1e%d\n", UnitTypeToString(axis.unit), axis.unit.exponent);
    } else {
      this->Print("Unit: %8s\n", UnitTypeToString(axis.unit));
    }
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
