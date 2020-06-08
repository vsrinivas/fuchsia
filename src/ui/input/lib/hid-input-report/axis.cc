// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/axis.h"

namespace hid_input_report {

fuchsia_input_report::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit) {
  fuchsia_input_report::Unit out_unit;
  out_unit.type = fuchsia_input_report::UnitType::NONE;
  out_unit.exponent = 0;

  switch (unit) {
    case hid::unit::UnitType::None:
      return out_unit;
    case hid::unit::UnitType::Distance:
      out_unit.type = fuchsia_input_report::UnitType::METERS;
      out_unit.exponent = -6;
      return out_unit;
    case hid::unit::UnitType::Weight:
      out_unit.type = fuchsia_input_report::UnitType::GRAMS;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Rotation:
      out_unit.type = fuchsia_input_report::UnitType::DEGREES;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::AngularVelocity:
      out_unit.type = fuchsia_input_report::UnitType::ENGLISH_ANGULAR_VELOCITY;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::LinearVelocity:
      out_unit.type = fuchsia_input_report::UnitType::SI_LINEAR_VELOCITY;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Acceleration:
      out_unit.type = fuchsia_input_report::UnitType::SI_LINEAR_ACCELERATION;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::MagneticFlux:
      out_unit.type = fuchsia_input_report::UnitType::WEBERS;
      out_unit.exponent = -6;
      return out_unit;
    case hid::unit::UnitType::Light:
      out_unit.type = fuchsia_input_report::UnitType::CANDELAS;
      out_unit.exponent = 0;
      return out_unit;
    case hid::unit::UnitType::Pressure:
      out_unit.type = fuchsia_input_report::UnitType::PASCALS;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Lux:
      out_unit.type = fuchsia_input_report::UnitType::LUX;
      out_unit.exponent = -6;
      return out_unit;
    default:
      out_unit.type = fuchsia_input_report::UnitType::OTHER;
      out_unit.exponent = 0;
      return out_unit;
  }
}

zx_status_t HidSensorUsageToLlcppSensorType(hid::usage::Sensor usage,
                                            fuchsia_input_report::SensorType* type) {
  switch (usage) {
    case hid::usage::Sensor::kAccelerationAxisX:
      *type = fuchsia_input_report::SensorType::ACCELEROMETER_X;
      break;
    case hid::usage::Sensor::kAccelerationAxisY:
      *type = fuchsia_input_report::SensorType::ACCELEROMETER_Y;
      break;
    case hid::usage::Sensor::kAccelerationAxisZ:
      *type = fuchsia_input_report::SensorType::ACCELEROMETER_Z;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisX:
      *type = fuchsia_input_report::SensorType::MAGNETOMETER_X;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisY:
      *type = fuchsia_input_report::SensorType::MAGNETOMETER_Y;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisZ:
      *type = fuchsia_input_report::SensorType::MAGNETOMETER_Z;
      break;
    case hid::usage::Sensor::kAngularVelocityX:
      *type = fuchsia_input_report::SensorType::GYROSCOPE_X;
      break;
    case hid::usage::Sensor::kAngularVelocityY:
      *type = fuchsia_input_report::SensorType::GYROSCOPE_Y;
      break;
    case hid::usage::Sensor::kAngularVelocityZ:
      *type = fuchsia_input_report::SensorType::GYROSCOPE_Z;
      break;
    case hid::usage::Sensor::kLightIlluminance:
      *type = fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE;
      break;
    case hid::usage::Sensor::kLightRedLight:
      *type = fuchsia_input_report::SensorType::LIGHT_RED;
      break;
    case hid::usage::Sensor::kLightBlueLight:
      *type = fuchsia_input_report::SensorType::LIGHT_BLUE;
      break;
    case hid::usage::Sensor::kLightGreenLight:
      *type = fuchsia_input_report::SensorType::LIGHT_GREEN;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t HidLedUsageToLlcppLedType(hid::usage::LEDs usage, fuchsia_input_report::LedType* type) {
  switch (usage) {
    case hid::usage::LEDs::kNumLock:
      *type = fuchsia_input_report::LedType::NUM_LOCK;
      break;
    case hid::usage::LEDs::kCapsLock:
      *type = fuchsia_input_report::LedType::CAPS_LOCK;
      break;
    case hid::usage::LEDs::kScrollLock:
      *type = fuchsia_input_report::LedType::SCROLL_LOCK;
      break;
    case hid::usage::LEDs::kCompose:
      *type = fuchsia_input_report::LedType::COMPOSE;
      break;
    case hid::usage::LEDs::kKana:
      *type = fuchsia_input_report::LedType::KANA;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

fuchsia_input_report::Axis LlcppAxisFromAttribute(const hid::Attributes& attrs) {
  fuchsia_input_report::Axis axis;
  axis.range.min = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.min)));
  axis.range.max = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.max)));
  axis.unit = HidUnitToLlcppUnit(hid::unit::GetUnitTypeFromUnit(attrs.unit));
  return axis;
}

}  // namespace hid_input_report
