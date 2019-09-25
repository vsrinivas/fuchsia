// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "descriptors.h"

#include <variant>

namespace hid_input_report_dev {

static llcpp_report::Unit HidUnitToLlcppUnit(hid::unit::UnitType unit) {
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
    default:
      return llcpp_report::Unit::OTHER;
  }
}

static llcpp_report::Axis HidAxisToLlcppAxis(hid_input_report::Axis axis) {
  llcpp_report::Axis new_axis = {};
  new_axis.range.min = axis.range.min;
  new_axis.range.max = axis.range.max;
  new_axis.unit = HidUnitToLlcppUnit(axis.unit);
  return new_axis;
}

zx_status_t SetMouseDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                               Descriptor* descriptor) {
  const auto& hid_mouse_desc = std::get<hid_input_report::MouseDescriptor>(hid_desc.descriptor);
  MouseDesc& mouse_desc = descriptor->mouse_desc;
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

  descriptor->mouse_desc.mouse_descriptor = mouse_builder.view();
  descriptor->descriptor = llcpp_report::DeviceDescriptor::Build();
  descriptor->descriptor.set_mouse(&descriptor->mouse_desc.mouse_descriptor);

  return ZX_OK;
}

zx_status_t SetMouseReport(hid_input_report::Report* hid_report, Report* report) {
  MouseReport& mouse_report = report->mouse_report;
  auto& hid_mouse_report = std::get<hid_input_report::MouseReport>(hid_report->report);
  auto& mouse_builder = report->mouse_report.mouse_builder;
  mouse_builder = llcpp_report::MouseReport::Build();

  if (hid_mouse_report.has_movement_x) {
    mouse_builder.set_movement_x(&hid_mouse_report.movement_x);
  }
  if (hid_mouse_report.has_movement_y) {
    mouse_builder.set_movement_y(&hid_mouse_report.movement_y);
  }
  mouse_report.buttons_view = fidl::VectorView<uint8_t>(hid_mouse_report.buttons_pressed,
                                                        hid_mouse_report.num_buttons_pressed);

  mouse_builder.set_pressed_buttons(&mouse_report.buttons_view);

  mouse_report.mouse_report = mouse_builder.view();
  report->report = llcpp_report::InputReport::Build();
  report->report.set_mouse(&report->mouse_report.mouse_report);

  return ZX_OK;
}

}  // namespace hid_input_report_dev
