// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/axis.h"

namespace hid_input_report {

fuchsia_input_report::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit) {
  switch (unit) {
    case hid::unit::UnitType::None:
      return fuchsia_input_report::Unit::NONE;
    case hid::unit::UnitType::Other:
      return fuchsia_input_report::Unit::OTHER;
    case hid::unit::UnitType::Distance:
      return fuchsia_input_report::Unit::DISTANCE;
    case hid::unit::UnitType::Weight:
      return fuchsia_input_report::Unit::WEIGHT;
    case hid::unit::UnitType::Rotation:
      return fuchsia_input_report::Unit::ROTATION;
    case hid::unit::UnitType::AngularVelocity:
      return fuchsia_input_report::Unit::ANGULAR_VELOCITY;
    case hid::unit::UnitType::LinearVelocity:
      return fuchsia_input_report::Unit::LINEAR_VELOCITY;
    case hid::unit::UnitType::Acceleration:
      return fuchsia_input_report::Unit::ACCELERATION;
    case hid::unit::UnitType::MagneticFlux:
      return fuchsia_input_report::Unit::MAGNETIC_FLUX;
    case hid::unit::UnitType::Light:
      return fuchsia_input_report::Unit::LUMINOUS_FLUX;
    case hid::unit::UnitType::Pressure:
      return fuchsia_input_report::Unit::PRESSURE;
    case hid::unit::UnitType::Lux:
      return fuchsia_input_report::Unit::LUX;
    default:
      return fuchsia_input_report::Unit::OTHER;
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
