// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/axis.h"

namespace hid_input_report {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

llcpp_report::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit) {
  switch (unit) {
    case hid::unit::UnitType::None:
      return llcpp_report::Unit::NONE;
    case hid::unit::UnitType::Other:
      return llcpp_report::Unit::OTHER;
    case hid::unit::UnitType::Distance:
      return llcpp_report::Unit::DISTANCE;
    case hid::unit::UnitType::Weight:
      return llcpp_report::Unit::WEIGHT;
    case hid::unit::UnitType::Rotation:
      return llcpp_report::Unit::ROTATION;
    case hid::unit::UnitType::AngularVelocity:
      return llcpp_report::Unit::ANGULAR_VELOCITY;
    case hid::unit::UnitType::LinearVelocity:
      return llcpp_report::Unit::LINEAR_VELOCITY;
    case hid::unit::UnitType::Acceleration:
      return llcpp_report::Unit::ACCELERATION;
    case hid::unit::UnitType::MagneticFlux:
      return llcpp_report::Unit::MAGNETIC_FLUX;
    case hid::unit::UnitType::Light:
      return llcpp_report::Unit::LUMINOUS_FLUX;
    case hid::unit::UnitType::Pressure:
      return llcpp_report::Unit::PRESSURE;
    case hid::unit::UnitType::Lux:
      return llcpp_report::Unit::LUX;
    default:
      return llcpp_report::Unit::OTHER;
  }
}

zx_status_t HidSensorUsageToLlcppSensorType(hid::usage::Sensor usage,
                                            llcpp_report::SensorType* type) {
  switch (usage) {
    case hid::usage::Sensor::kAccelerationAxisX:
      *type = llcpp_report::SensorType::ACCELEROMETER_X;
      break;
    case hid::usage::Sensor::kAccelerationAxisY:
      *type = llcpp_report::SensorType::ACCELEROMETER_Y;
      break;
    case hid::usage::Sensor::kAccelerationAxisZ:
      *type = llcpp_report::SensorType::ACCELEROMETER_Z;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisX:
      *type = llcpp_report::SensorType::MAGNETOMETER_X;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisY:
      *type = llcpp_report::SensorType::MAGNETOMETER_Y;
      break;
    case hid::usage::Sensor::kMagneticFluxAxisZ:
      *type = llcpp_report::SensorType::MAGNETOMETER_Z;
      break;
    case hid::usage::Sensor::kAngularVelocityX:
      *type = llcpp_report::SensorType::GYROSCOPE_X;
      break;
    case hid::usage::Sensor::kAngularVelocityY:
      *type = llcpp_report::SensorType::GYROSCOPE_Y;
      break;
    case hid::usage::Sensor::kAngularVelocityZ:
      *type = llcpp_report::SensorType::GYROSCOPE_Z;
      break;
    case hid::usage::Sensor::kLightIlluminance:
      *type = llcpp_report::SensorType::LIGHT_ILLUMINANCE;
      break;
    case hid::usage::Sensor::kLightRedLight:
      *type = llcpp_report::SensorType::LIGHT_RED;
      break;
    case hid::usage::Sensor::kLightBlueLight:
      *type = llcpp_report::SensorType::LIGHT_BLUE;
      break;
    case hid::usage::Sensor::kLightGreenLight:
      *type = llcpp_report::SensorType::LIGHT_GREEN;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

llcpp_report::Axis LlcppAxisFromAttribute(const hid::Attributes& attrs) {
  llcpp_report::Axis axis;
  axis.range.min = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.min)));
  axis.range.max = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.max)));
  axis.unit = HidUnitToLlcppUnit(hid::unit::GetUnitTypeFromUnit(attrs.unit));
  return axis;
}

}  // namespace hid_input_report
