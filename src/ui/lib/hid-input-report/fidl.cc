// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/fidl.h"

#include <stdio.h>

#include <variant>

#include <hid-parser/usages.h>

#include "src/ui/lib/hid-input-report/axis.h"
#include "src/ui/lib/key_util/key_util.h"

namespace hid_input_report {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

void SetMouseDescriptor(const MouseDescriptor& hid_mouse_desc, FidlMouseDescriptor* descriptor) {
  descriptor->data = hid_mouse_desc;
  if (descriptor->data.movement_x) {
    descriptor->builder.set_movement_x(&descriptor->data.movement_x.value());
  }
  if (descriptor->data.movement_y) {
    descriptor->builder.set_movement_y(&descriptor->data.movement_y.value());
  }

  descriptor->buttons_view =
      fidl::VectorView<uint8_t>(descriptor->data.buttons.data(), descriptor->data.num_buttons);
  descriptor->builder.set_buttons(&descriptor->buttons_view);

  descriptor->descriptor = descriptor->builder.view();
}

void SetMouseReport(const MouseReport& hid_mouse_report, FidlMouseReport* report) {
  report->data = hid_mouse_report;

  if (hid_mouse_report.has_movement_x) {
    report->builder.set_movement_x(&report->data.movement_x);
  }
  if (hid_mouse_report.has_movement_y) {
    report->builder.set_movement_y(&report->data.movement_y);
  }
  report->buttons_view =
      fidl::VectorView<uint8_t>(report->data.buttons_pressed, report->data.num_buttons_pressed);

  report->builder.set_pressed_buttons(&report->buttons_view);

  report->report = report->builder.view();
}

void SetSensorDescriptor(const SensorDescriptor& hid_sensor_desc,
                         FidlSensorDescriptor* descriptor) {
  descriptor->data = hid_sensor_desc;
  descriptor->values_view = fidl::VectorView<llcpp_report::SensorAxis>(
      descriptor->data.values.data(), descriptor->data.num_values);
  descriptor->builder.set_values(&descriptor->values_view);
  descriptor->descriptor = descriptor->builder.view();
}

void SetSensorReport(const SensorReport& hid_sensor_report, FidlSensorReport* report) {
  report->data = hid_sensor_report;

  report->values_view = fidl::VectorView<int64_t>(report->data.values, report->data.num_values);
  report->builder.set_values(&report->values_view);

  report->report = report->builder.view();
}

void SetTouchDescriptor(const TouchDescriptor& hid_touch_desc, FidlTouchDescriptor* descriptor) {
  descriptor->data = hid_touch_desc;
  for (size_t i = 0; i < descriptor->data.num_contacts; i++) {
    llcpp_report::ContactDescriptor::Builder& contact_builder =
        descriptor->contacts_builder[i].builder;
    if (descriptor->data.contacts[i].position_x) {
      contact_builder.set_position_x(&descriptor->data.contacts[i].position_x.value());
    }
    if (descriptor->data.contacts[i].position_y) {
      contact_builder.set_position_y(&descriptor->data.contacts[i].position_y.value());
    }
    if (descriptor->data.contacts[i].pressure) {
      contact_builder.set_pressure(&descriptor->data.contacts[i].pressure.value());
    }
    if (descriptor->data.contacts[i].contact_width) {
      contact_builder.set_contact_width(&descriptor->data.contacts[i].contact_width.value());
    }
    if (descriptor->data.contacts[i].contact_height) {
      contact_builder.set_contact_height(&descriptor->data.contacts[i].contact_height.value());
    }
    descriptor->contacts_built[i] = contact_builder.view();
  }

  descriptor->contacts_view = fidl::VectorView<llcpp_report::ContactDescriptor>(
      descriptor->contacts_built.data(), descriptor->data.num_contacts);

  descriptor->builder.set_contacts(&descriptor->contacts_view);
  descriptor->builder.set_max_contacts(&descriptor->data.max_contacts);
  descriptor->builder.set_touch_type(&descriptor->data.touch_type);

  descriptor->descriptor = descriptor->builder.view();
}

void SetTouchReport(const TouchReport& hid_touch_report, FidlTouchReport* report) {
  report->data = hid_touch_report;

  for (size_t i = 0; i < report->data.num_contacts; i++) {
    llcpp_report::ContactReport::Builder& contact_builder = report->contacts[i].builder;
    ContactReport& contact = report->data.contacts[i];

    if (contact.has_contact_id) {
      contact_builder.set_contact_id(&contact.contact_id);
    }
    if (contact.has_position_x) {
      contact_builder.set_position_x(&contact.position_x);
    }
    if (contact.has_position_y) {
      contact_builder.set_position_y(&contact.position_y);
    }
    if (contact.has_pressure) {
      contact_builder.set_pressure(&contact.pressure);
    }
    if (contact.has_contact_width) {
      contact_builder.set_contact_width(&contact.contact_width);
    }
    if (contact.has_contact_height) {
      contact_builder.set_contact_height(&contact.contact_height);
    }
    report->contacts_built[i] = contact_builder.view();
  }

  report->contacts_view = fidl::VectorView<llcpp_report::ContactReport>(
      report->contacts_built.data(), hid_touch_report.num_contacts);
  report->builder.set_contacts(&report->contacts_view);

  report->report = report->builder.view();
}

void SetKeyboardDescriptor(const KeyboardDescriptor& hid_keyboard_desc,
                           FidlKeyboardDescriptor* descriptor) {
  descriptor->data = hid_keyboard_desc;
  descriptor->keys_view = fidl::VectorView<llcpp::fuchsia::ui::input2::Key>(
      descriptor->data.keys.data(), descriptor->data.num_keys);
  descriptor->builder.set_keys(&descriptor->keys_view);
  descriptor->descriptor = descriptor->builder.view();
}

void SetKeyboardReport(const KeyboardReport& hid_keyboard_report, FidlKeyboardReport* report) {
  size_t fidl_key_index = 0;
  for (size_t i = 0; i < hid_keyboard_report.num_pressed_keys; i++) {
    std::optional<fuchsia::ui::input2::Key> key = key_util::hid_key_to_fuchsia_key(
        hid::USAGE(hid::usage::Page::kKeyboardKeypad, hid_keyboard_report.pressed_keys[i]));
    if (key) {
      // Cast the key enum from HLCPP to LLCPP. We are guaranteed that this will be equivalent.
      report->pressed_keys_data[fidl_key_index++] =
          static_cast<llcpp::fuchsia::ui::input2::Key>(*key);
    }
  }
  report->pressed_keys_view = fidl::VectorView<llcpp::fuchsia::ui::input2::Key>(
      report->pressed_keys_data.data(), fidl_key_index);
  report->builder.set_pressed_keys(&report->pressed_keys_view);
  report->report = report->builder.view();
}

zx_status_t SetFidlDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                              FidlDescriptor* descriptor) {
  if (std::holds_alternative<MouseDescriptor>(hid_desc.descriptor)) {
    SetMouseDescriptor(std::get<MouseDescriptor>(hid_desc.descriptor), &descriptor->mouse);
    descriptor->builder.set_mouse(&descriptor->mouse.descriptor);
    return ZX_OK;
  }
  if (std::holds_alternative<SensorDescriptor>(hid_desc.descriptor)) {
    SetSensorDescriptor(std::get<SensorDescriptor>(hid_desc.descriptor), &descriptor->sensor);
    descriptor->builder.set_sensor(&descriptor->sensor.descriptor);
    return ZX_OK;
  }
  if (std::holds_alternative<TouchDescriptor>(hid_desc.descriptor)) {
    SetTouchDescriptor(std::get<TouchDescriptor>(hid_desc.descriptor), &descriptor->touch);
    descriptor->builder.set_touch(&descriptor->touch.descriptor);
    return ZX_OK;
  }
  if (std::holds_alternative<KeyboardDescriptor>(hid_desc.descriptor)) {
    SetKeyboardDescriptor(std::get<KeyboardDescriptor>(hid_desc.descriptor), &descriptor->keyboard);
    descriptor->builder.set_keyboard(&descriptor->keyboard.descriptor);
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SetFidlReport(const hid_input_report::Report& hid_report, FidlReport* report) {
  if (std::holds_alternative<MouseReport>(hid_report.report)) {
    report->report = FidlMouseReport();
    FidlMouseReport* mouse = &std::get<FidlMouseReport>(report->report);
    SetMouseReport(std::get<MouseReport>(hid_report.report), mouse);
    report->builder.set_mouse(&mouse->report);
    return ZX_OK;
  }
  if (std::holds_alternative<SensorReport>(hid_report.report)) {
    report->report = FidlSensorReport();
    FidlSensorReport* sensor = &std::get<FidlSensorReport>(report->report);
    SetSensorReport(std::get<SensorReport>(hid_report.report), sensor);
    report->builder.set_sensor(&sensor->report);
    return ZX_OK;
  }
  if (std::holds_alternative<TouchReport>(hid_report.report)) {
    report->report = FidlTouchReport();
    FidlTouchReport* touch = &std::get<FidlTouchReport>(report->report);
    SetTouchReport(std::get<TouchReport>(hid_report.report), touch);
    report->builder.set_touch(&touch->report);
    return ZX_OK;
  }
  if (std::holds_alternative<KeyboardReport>(hid_report.report)) {
    report->report = FidlKeyboardReport();
    FidlKeyboardReport* keyboard = &std::get<FidlKeyboardReport>(report->report);
    SetKeyboardReport(std::get<KeyboardReport>(hid_report.report), keyboard);
    report->builder.set_keyboard(&keyboard->report);
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace hid_input_report
