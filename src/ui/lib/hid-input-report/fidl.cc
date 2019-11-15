// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/fidl.h"

#include <stdio.h>

#include <variant>

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

zx_status_t SetTouchDescriptor(const TouchDescriptor& hid_touch_desc, FidlDescriptor* descriptor) {
  FidlTouchDescriptor& touch_desc = descriptor->touch_descriptor;
  auto& touch_builder = touch_desc.touch_builder;

  for (size_t i = 0; i < hid_touch_desc.num_contacts; i++) {
    FidlContactDescriptor& contact = touch_desc.contacts[i];

    if (hid_touch_desc.contacts[i].position_x.enabled) {
      contact.position_x = HidAxisToLlcppAxis(hid_touch_desc.contacts[i].position_x);
      contact.contact_builder.set_position_x(&touch_desc.contacts[i].position_x);
    }
    if (hid_touch_desc.contacts[i].position_y.enabled) {
      contact.position_y = HidAxisToLlcppAxis(hid_touch_desc.contacts[i].position_y);
      contact.contact_builder.set_position_y(&touch_desc.contacts[i].position_y);
    }
    if (hid_touch_desc.contacts[i].pressure.enabled) {
      contact.pressure = HidAxisToLlcppAxis(hid_touch_desc.contacts[i].pressure);
      contact.contact_builder.set_pressure(&touch_desc.contacts[i].pressure);
    }
    if (hid_touch_desc.contacts[i].contact_width.enabled) {
      contact.contact_width = HidAxisToLlcppAxis(hid_touch_desc.contacts[i].contact_width);
      contact.contact_builder.set_contact_width(&touch_desc.contacts[i].contact_width);
    }
    if (hid_touch_desc.contacts[i].contact_height.enabled) {
      contact.contact_height = HidAxisToLlcppAxis(hid_touch_desc.contacts[i].contact_height);
      contact.contact_builder.set_contact_height(&touch_desc.contacts[i].contact_height);
    }
    touch_desc.contacts_built[i] = contact.contact_builder.view();
  }
  touch_desc.contacts_view = fidl::VectorView<llcpp_report::ContactDescriptor>(
      touch_desc.contacts_built.data(), hid_touch_desc.num_contacts);
  touch_builder.set_contacts(&touch_desc.contacts_view);

  touch_desc.max_contacts = hid_touch_desc.max_contacts;
  touch_builder.set_max_contacts(&touch_desc.max_contacts);

  touch_desc.touch_type = hid_touch_desc.touch_type;
  touch_builder.set_touch_type(&touch_desc.touch_type);

  descriptor->touch_descriptor.touch_descriptor = touch_builder.view();
  descriptor->descriptor_builder.set_touch(&descriptor->touch_descriptor.touch_descriptor);

  return ZX_OK;
}

zx_status_t SetTouchReport(const TouchReport& hid_touch_report, FidlReport* report) {
  FidlTouchReport& touch_report = std::get<FidlTouchReport>(report->report);
  auto& touch_builder = touch_report.touch_builder;

  touch_report.report_data = hid_touch_report;
  TouchReport& report_data = touch_report.report_data;

  for (size_t i = 0; i < report_data.num_contacts; i++) {
    llcpp_report::ContactReport::Builder& fidl_contact = touch_report.contacts[i].contact;
    fidl_contact = llcpp_report::ContactReport::Build();

    ContactReport& contact = report_data.contacts[i];

    if (contact.has_contact_id) {
      fidl_contact.set_contact_id(&contact.contact_id);
    }
    if (contact.has_position_x) {
      fidl_contact.set_position_x(&contact.position_x);
    }
    if (contact.has_position_y) {
      fidl_contact.set_position_y(&contact.position_y);
    }
    if (contact.has_pressure) {
      fidl_contact.set_pressure(&contact.pressure);
    }
    if (contact.has_contact_width) {
      fidl_contact.set_contact_width(&contact.contact_width);
    }
    if (contact.has_contact_height) {
      fidl_contact.set_contact_height(&contact.contact_height);
    }
    touch_report.contacts_built[i] = fidl_contact.view();
  }

  touch_report.contacts_view = fidl::VectorView<llcpp_report::ContactReport>(
      touch_report.contacts_built.data(), hid_touch_report.num_contacts);
  touch_builder.set_contacts(&touch_report.contacts_view);

  touch_report.touch_report = touch_builder.view();
  report->report_builder.set_touch(&touch_report.touch_report);

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
  if (std::holds_alternative<TouchDescriptor>(hid_desc.descriptor)) {
    return SetTouchDescriptor(std::get<TouchDescriptor>(hid_desc.descriptor), descriptor);
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
  if (std::holds_alternative<TouchReport>(hid_report.report)) {
    report->report = FidlTouchReport();
    return SetTouchReport(std::get<TouchReport>(hid_report.report), report);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace hid_input_report
