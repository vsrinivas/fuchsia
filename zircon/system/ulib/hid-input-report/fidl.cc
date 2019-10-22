// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <variant>

#include <hid-input-report/fidl.h>
#include <hid-parser/usages.h>
namespace hid_input_report {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

namespace {

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

llcpp_report::Axis HidAxisToLlcppAxis(Axis axis) {
  llcpp_report::Axis new_axis = {};
  new_axis.range.min = axis.range.min;
  new_axis.range.max = axis.range.max;
  new_axis.unit = HidUnitToLlcppUnit(axis.unit);
  return new_axis;
}

}  // namespace

zx_status_t SetMouseDescriptor(const MouseDescriptor& hid_mouse_desc, FidlDescriptor* descriptor) {
  FidlMouseDescriptor& mouse_desc = descriptor->mouse_descriptor;
  auto& mouse_builder = mouse_desc.mouse_builder;
  mouse_builder = llcpp_report::MouseDescriptor::Build();

  if (hid_mouse_desc.movement_x.enabled) {
    mouse_desc.movement_x = HidAxisToLlcppAxis(hid_mouse_desc.movement_x);
    mouse_builder.set_movement_x(&mouse_desc.movement_x);
  }
  if (hid_mouse_desc.movement_y.enabled) {
    mouse_desc.movement_y = HidAxisToLlcppAxis(hid_mouse_desc.movement_y);
    mouse_builder.set_movement_y(&mouse_desc.movement_y);
  }

  for (size_t i = 0; i < hid_mouse_desc.num_buttons; i++) {
    mouse_desc.buttons[i] = hid_mouse_desc.button_ids[i];
  }
  mouse_desc.buttons_view =
      fidl::VectorView<uint8_t>(mouse_desc.buttons, hid_mouse_desc.num_buttons);
  mouse_builder.set_buttons(&mouse_desc.buttons_view);

  descriptor->mouse_descriptor.mouse_descriptor = mouse_builder.view();
  descriptor->descriptor_builder.set_mouse(&descriptor->mouse_descriptor.mouse_descriptor);

  return ZX_OK;
}

zx_status_t SetMouseReport(const MouseReport& hid_mouse_report, FidlReport* report) {
  FidlMouseReport& mouse_report = std::get<FidlMouseReport>(report->report);
  auto& mouse_builder = mouse_report.mouse_builder;
  mouse_builder = llcpp_report::MouseReport::Build();

  mouse_report.report_data = hid_mouse_report;
  MouseReport& report_data = mouse_report.report_data;

  if (hid_mouse_report.has_movement_x) {
    mouse_builder.set_movement_x(&report_data.movement_x);
  }
  if (hid_mouse_report.has_movement_y) {
    mouse_builder.set_movement_y(&report_data.movement_y);
  }
  mouse_report.buttons_view =
      fidl::VectorView<uint8_t>(report_data.buttons_pressed, report_data.num_buttons_pressed);

  mouse_builder.set_pressed_buttons(&mouse_report.buttons_view);

  mouse_report.mouse_report = mouse_builder.view();
  report->report_builder.set_mouse(&mouse_report.mouse_report);

  return ZX_OK;
}

zx_status_t SetSensorDescriptor(const SensorDescriptor& hid_sensor_desc,
                                FidlDescriptor* descriptor) {
  FidlSensorDescriptor& sensor_desc = descriptor->sensor_descriptor;
  auto& sensor_builder = sensor_desc.sensor_builder;

  size_t fidl_value_index = 0;
  for (size_t i = 0; i < hid_sensor_desc.num_values; i++) {
    zx_status_t status = HidSensorUsageToLlcppSensorType(
        hid_sensor_desc.values[i].type, &sensor_desc.values[fidl_value_index].type);
    if (status != ZX_OK) {
      continue;
    }
    sensor_desc.values[fidl_value_index].axis = HidAxisToLlcppAxis(hid_sensor_desc.values[i].axis);
    fidl_value_index++;
  }

  sensor_desc.values_view =
      fidl::VectorView<llcpp_report::SensorAxis>(sensor_desc.values.data(), fidl_value_index);
  sensor_builder.set_values(&sensor_desc.values_view);

  descriptor->sensor_descriptor.sensor_descriptor = sensor_builder.view();
  descriptor->descriptor_builder.set_sensor(&descriptor->sensor_descriptor.sensor_descriptor);

  return ZX_OK;
}

zx_status_t SetSensorReport(const SensorReport& hid_sensor_report, FidlReport* report) {
  FidlSensorReport& sensor_report = std::get<FidlSensorReport>(report->report);
  auto& sensor_builder = sensor_report.sensor_builder;
  sensor_builder = llcpp_report::SensorReport::Build();

  sensor_report.report_data = hid_sensor_report;

  sensor_report.values_view = fidl::VectorView<int64_t>(sensor_report.report_data.values,
                                                        sensor_report.report_data.num_values);
  sensor_builder.set_values(&sensor_report.values_view);

  sensor_report.sensor_report = sensor_builder.view();
  report->report_builder.set_sensor(&sensor_report.sensor_report);

  return ZX_OK;
}

zx_status_t SetFidlDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                              FidlDescriptor* descriptor) {
  if (std::holds_alternative<MouseDescriptor>(hid_desc.descriptor)) {
    return SetMouseDescriptor(std::get<MouseDescriptor>(hid_desc.descriptor), descriptor);
  }
  if (std::holds_alternative<SensorDescriptor>(hid_desc.descriptor)) {
    return SetSensorDescriptor(std::get<SensorDescriptor>(hid_desc.descriptor), descriptor);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SetFidlReport(const hid_input_report::Report& hid_report, FidlReport* report) {
  if (std::holds_alternative<MouseReport>(hid_report.report)) {
    report->report = FidlMouseReport();
    return SetMouseReport(std::get<MouseReport>(hid_report.report), report);
  }
  if (std::holds_alternative<SensorReport>(hid_report.report)) {
    report->report = FidlSensorReport();
    return SetSensorReport(std::get<SensorReport>(hid_report.report), report);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace hid_input_report
