// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/axis.h"

namespace hid_input_report {
fuchsia_input_report::wire::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit) {
  fuchsia_input_report::wire::Unit out_unit;
  out_unit.type = fuchsia_input_report::wire::UnitType::kNone;
  out_unit.exponent = 0;

  switch (unit) {
    case hid::unit::UnitType::None:
      return out_unit;
    case hid::unit::UnitType::Distance:
      out_unit.type = fuchsia_input_report::wire::UnitType::kMeters;
      out_unit.exponent = -6;
      return out_unit;
    case hid::unit::UnitType::Weight:
      out_unit.type = fuchsia_input_report::wire::UnitType::kGrams;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Rotation:
      out_unit.type = fuchsia_input_report::wire::UnitType::kDegrees;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::AngularVelocity:
      out_unit.type = fuchsia_input_report::wire::UnitType::kEnglishAngularVelocity;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::LinearVelocity:
      out_unit.type = fuchsia_input_report::wire::UnitType::kSiLinearVelocity;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Acceleration:
      out_unit.type = fuchsia_input_report::wire::UnitType::kSiLinearAcceleration;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::MagneticFlux:
      out_unit.type = fuchsia_input_report::wire::UnitType::kWebers;
      out_unit.exponent = -6;
      return out_unit;
    case hid::unit::UnitType::Light:
      out_unit.type = fuchsia_input_report::wire::UnitType::kCandelas;
      out_unit.exponent = 0;
      return out_unit;
    case hid::unit::UnitType::Pressure:
      out_unit.type = fuchsia_input_report::wire::UnitType::kPascals;
      out_unit.exponent = -3;
      return out_unit;
    case hid::unit::UnitType::Lux:
      out_unit.type = fuchsia_input_report::wire::UnitType::kLux;
      out_unit.exponent = -6;
      return out_unit;
    default:
      out_unit.type = fuchsia_input_report::wire::UnitType::kOther;
      out_unit.exponent = 0;
      return out_unit;
  }
}

zx_status_t HidSensorUsageToLlcppSensorType(hid::usage::Sensor usage,
                                            fuchsia_input_report::wire::SensorType* type) {
  switch (usage) {
    case hid::usage::Sensor::kAccelerationAxisX:
      *type = fuchsia_input_report::wire::SensorType::kAccelerometerX;
      break;
    case hid::usage::Sensor::kAccelerationAxisY:
      *type = fuchsia_input_report::wire::SensorType::kAccelerometerY;
      break;
    case hid::usage::Sensor::kAccelerationAxisZ:
      *type = fuchsia_input_report::wire::SensorType::kAccelerometerZ;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisX:
      *type = fuchsia_input_report::wire::SensorType::kMagnetometerX;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisY:
      *type = fuchsia_input_report::wire::SensorType::kMagnetometerY;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisZ:
      *type = fuchsia_input_report::wire::SensorType::kMagnetometerZ;
      break;
    case hid::usage::Sensor::kAngularVelocityX:
      *type = fuchsia_input_report::wire::SensorType::kGyroscopeX;
      break;
    case hid::usage::Sensor::kAngularVelocityY:
      *type = fuchsia_input_report::wire::SensorType::kGyroscopeY;
      break;
    case hid::usage::Sensor::kAngularVelocityZ:
      *type = fuchsia_input_report::wire::SensorType::kGyroscopeZ;
      break;
    case hid::usage::Sensor::kLightIlluminance:
      *type = fuchsia_input_report::wire::SensorType::kLightIlluminance;
      break;
    case hid::usage::Sensor::kLightRedLight:
      *type = fuchsia_input_report::wire::SensorType::kLightRed;
      break;
    case hid::usage::Sensor::kLightBlueLight:
      *type = fuchsia_input_report::wire::SensorType::kLightBlue;
      break;
    case hid::usage::Sensor::kLightGreenLight:
      *type = fuchsia_input_report::wire::SensorType::kLightGreen;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t HidLedUsageToLlcppLedType(hid::usage::LEDs usage,
                                      fuchsia_input_report::wire::LedType* type) {
  switch (usage) {
    case hid::usage::LEDs::kNumLock:
      *type = fuchsia_input_report::wire::LedType::kNumLock;
      break;
    case hid::usage::LEDs::kCapsLock:
      *type = fuchsia_input_report::wire::LedType::kCapsLock;
      break;
    case hid::usage::LEDs::kScrollLock:
      *type = fuchsia_input_report::wire::LedType::kScrollLock;
      break;
    case hid::usage::LEDs::kCompose:
      *type = fuchsia_input_report::wire::LedType::kCompose;
      break;
    case hid::usage::LEDs::kKana:
      *type = fuchsia_input_report::wire::LedType::kKana;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

fuchsia_input_report::wire::Axis LlcppAxisFromAttribute(const hid::Attributes& attrs) {
  fuchsia_input_report::wire::Axis axis;
  axis.range.min = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.min)));
  axis.range.max = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.max)));
  axis.unit = HidUnitToLlcppUnit(hid::unit::GetUnitTypeFromUnit(attrs.unit));
  return axis;
}

}  // namespace hid_input_report
