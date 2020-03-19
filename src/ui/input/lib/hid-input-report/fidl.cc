// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/fidl.h"

#include <stdio.h>

#include <variant>

#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/axis.h"
#include "src/ui/lib/key_util/key_util.h"

namespace hid_input_report {

void SetMouseInputDescriptor(FidlMouseInputDescriptor* descriptor) {
  if (descriptor->data.movement_x) {
    descriptor->builder.set_movement_x(fidl::unowned_ptr(&descriptor->data.movement_x.value()));
  }
  if (descriptor->data.movement_y) {
    descriptor->builder.set_movement_y(fidl::unowned_ptr(&descriptor->data.movement_y.value()));
  }
  if (descriptor->data.position_x) {
    descriptor->builder.set_position_x(fidl::unowned_ptr(&descriptor->data.position_x.value()));
  }
  if (descriptor->data.position_y) {
    descriptor->builder.set_position_y(fidl::unowned_ptr(&descriptor->data.position_y.value()));
  }

  descriptor->buttons_view = fidl::VectorView<uint8_t>(
      fidl::unowned_ptr(descriptor->data.buttons.data()), descriptor->data.num_buttons);
  descriptor->builder.set_buttons(fidl::unowned_ptr(&descriptor->buttons_view));

  descriptor->descriptor = descriptor->builder.build();
}

void SetMouseInputReport(FidlMouseInputReport* report) {
  if (report->data.movement_x) {
    report->builder.set_movement_x(fidl::unowned_ptr(&report->data.movement_x.value()));
  }
  if (report->data.movement_y) {
    report->builder.set_movement_y(fidl::unowned_ptr(&report->data.movement_y.value()));
  }
  if (report->data.position_x) {
    report->builder.set_position_x(fidl::unowned_ptr(&report->data.position_x.value()));
  }
  if (report->data.position_y) {
    report->builder.set_position_y(fidl::unowned_ptr(&report->data.position_y.value()));
  }
  if (report->data.scroll_v) {
    report->builder.set_scroll_v(fidl::unowned_ptr(&report->data.scroll_v.value()));
  }
  if (report->data.scroll_h) {
    report->builder.set_scroll_h(fidl::unowned_ptr(&report->data.scroll_h.value()));
  }
  report->buttons_view = fidl::VectorView<uint8_t>(
      fidl::unowned_ptr(report->data.buttons_pressed.data()), report->data.num_buttons_pressed);

  report->builder.set_pressed_buttons(fidl::unowned_ptr(&report->buttons_view));

  report->report = report->builder.build();
}

void SetSensorInputDescriptor(FidlSensorInputDescriptor* descriptor) {
  descriptor->values_view = fidl::VectorView<fuchsia_input_report::SensorAxis>(
      fidl::unowned_ptr(descriptor->data.values.data()), descriptor->data.num_values);
  descriptor->builder.set_values(fidl::unowned_ptr(&descriptor->values_view));
  descriptor->descriptor = descriptor->builder.build();
}

void SetSensorInputReport(FidlSensorInputReport* report) {
  report->values_view = fidl::VectorView<int64_t>(fidl::unowned_ptr(report->data.values.data()),
                                                  report->data.num_values);
  report->builder.set_values(fidl::unowned_ptr(&report->values_view));

  report->report = report->builder.build();
}

void SetTouchInputDescriptor(FidlTouchInputDescriptor* descriptor) {
  for (size_t i = 0; i < descriptor->data.num_contacts; i++) {
    fuchsia_input_report::ContactInputDescriptor::UnownedBuilder& contact_builder =
        descriptor->contacts_builder[i].builder;
    if (descriptor->data.contacts[i].position_x) {
      contact_builder.set_position_x(
          fidl::unowned_ptr(&descriptor->data.contacts[i].position_x.value()));
    }
    if (descriptor->data.contacts[i].position_y) {
      contact_builder.set_position_y(
          fidl::unowned_ptr(&descriptor->data.contacts[i].position_y.value()));
    }
    if (descriptor->data.contacts[i].pressure) {
      contact_builder.set_pressure(
          fidl::unowned_ptr(&descriptor->data.contacts[i].pressure.value()));
    }
    if (descriptor->data.contacts[i].contact_width) {
      contact_builder.set_contact_width(
          fidl::unowned_ptr(&descriptor->data.contacts[i].contact_width.value()));
    }
    if (descriptor->data.contacts[i].contact_height) {
      contact_builder.set_contact_height(
          fidl::unowned_ptr(&descriptor->data.contacts[i].contact_height.value()));
    }
    descriptor->contacts_built[i] = contact_builder.build();
  }

  descriptor->contacts_view = fidl::VectorView<fuchsia_input_report::ContactInputDescriptor>(
      fidl::unowned_ptr(descriptor->contacts_built.data()), descriptor->data.num_contacts);
  descriptor->builder.set_contacts(fidl::unowned_ptr(&descriptor->contacts_view));

  descriptor->buttons_view = fidl::VectorView<uint8_t>(
      fidl::unowned_ptr(descriptor->data.buttons.data()), descriptor->data.num_buttons);
  descriptor->builder.set_buttons(fidl::unowned_ptr(&descriptor->buttons_view));

  descriptor->builder.set_max_contacts(fidl::unowned_ptr(&descriptor->data.max_contacts));
  descriptor->builder.set_touch_type(fidl::unowned_ptr(&descriptor->data.touch_type));

  descriptor->descriptor = descriptor->builder.build();
}

void SetTouchInputReport(FidlTouchInputReport* report) {
  for (size_t i = 0; i < report->data.num_contacts; i++) {
    fuchsia_input_report::ContactInputReport::UnownedBuilder& contact_builder =
        report->contacts[i].builder;
    ContactInputReport& contact = report->data.contacts[i];

    if (contact.contact_id) {
      contact_builder.set_contact_id(fidl::unowned_ptr(&contact.contact_id.value()));
    }
    if (contact.position_x) {
      contact_builder.set_position_x(fidl::unowned_ptr(&contact.position_x.value()));
    }
    if (contact.position_y) {
      contact_builder.set_position_y(fidl::unowned_ptr(&contact.position_y.value()));
    }
    if (contact.pressure) {
      contact_builder.set_pressure(fidl::unowned_ptr(&contact.pressure.value()));
    }
    if (contact.contact_width) {
      contact_builder.set_contact_width(fidl::unowned_ptr(&contact.contact_width.value()));
    }
    if (contact.contact_height) {
      contact_builder.set_contact_height(fidl::unowned_ptr(&contact.contact_height.value()));
    }
    report->contacts_built[i] = contact_builder.build();
  }

  report->contacts_view = fidl::VectorView<fuchsia_input_report::ContactInputReport>(
      fidl::unowned_ptr(report->contacts_built.data()), report->data.num_contacts);
  report->builder.set_contacts(fidl::unowned_ptr(&report->contacts_view));

  report->pressed_buttons_view = fidl::VectorView<uint8_t>(
      fidl::unowned_ptr(report->data.pressed_buttons.data()), report->data.num_pressed_buttons);
  report->builder.set_pressed_buttons(fidl::unowned_ptr(&report->pressed_buttons_view));

  report->report = report->builder.build();
}

void SetKeyboardInputDescriptor(FidlKeyboardInputDescriptor* descriptor) {
  descriptor->keys_view = fidl::VectorView<llcpp::fuchsia::ui::input2::Key>(
      fidl::unowned_ptr(descriptor->data.keys.data()), descriptor->data.num_keys);
  descriptor->builder.set_keys(fidl::unowned_ptr(&descriptor->keys_view));
  descriptor->descriptor = descriptor->builder.build();
}

void SetKeyboardOutputDescriptor(FidlKeyboardOutputDescriptor* descriptor) {
  descriptor->leds_view = fidl::VectorView<fuchsia_input_report::LedType>(
      fidl::unowned_ptr(descriptor->data.leds.data()), descriptor->data.num_leds);
  descriptor->builder.set_leds(fidl::unowned_ptr(&descriptor->leds_view));
  descriptor->descriptor = descriptor->builder.build();
}

void SetKeyboardInputReport(FidlKeyboardInputReport* report) {
  report->pressed_keys_view = fidl::VectorView<llcpp::fuchsia::ui::input2::Key>(
      fidl::unowned_ptr(report->data.pressed_keys.data()), report->data.num_pressed_keys);
  report->builder.set_pressed_keys(fidl::unowned_ptr(&report->pressed_keys_view));
  report->report = report->builder.build();
}

void SetConsumerControlInputDescriptor(FidlConsumerControlInputDescriptor* descriptor) {
  descriptor->buttons_view = fidl::VectorView<fuchsia_input_report::ConsumerControlButton>(
      fidl::unowned_ptr(descriptor->data.buttons.data()), descriptor->data.num_buttons);
  descriptor->builder.set_buttons(fidl::unowned_ptr(&descriptor->buttons_view));
  descriptor->descriptor = descriptor->builder.build();
}

void SetConsumerControlInputReport(FidlConsumerControlInputReport* report) {
  report->pressed_buttons_view = fidl::VectorView<fuchsia_input_report::ConsumerControlButton>(
      fidl::unowned_ptr(report->data.pressed_buttons.data()), report->data.num_pressed_buttons);
  report->builder.set_pressed_buttons(fidl::unowned_ptr(&report->pressed_buttons_view));
  report->report = report->builder.build();
}

zx_status_t SetFidlDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                              FidlDescriptor* descriptor) {
  if (std::holds_alternative<MouseDescriptor>(hid_desc.descriptor)) {
    const MouseDescriptor* hid_mouse = &std::get<MouseDescriptor>(hid_desc.descriptor);
    if (hid_mouse->input) {
      descriptor->mouse.input.data = *hid_mouse->input;
      SetMouseInputDescriptor(&descriptor->mouse.input);
      descriptor->mouse.builder.set_input(fidl::unowned_ptr(&descriptor->mouse.input.descriptor));
    }
    descriptor->mouse.descriptor = descriptor->mouse.builder.build();
    descriptor->builder.set_mouse(fidl::unowned_ptr(&descriptor->mouse.descriptor));
    return ZX_OK;
  }
  if (std::holds_alternative<SensorDescriptor>(hid_desc.descriptor)) {
    const SensorDescriptor* hid_sensor = &std::get<SensorDescriptor>(hid_desc.descriptor);
    if (hid_sensor->input) {
      descriptor->sensor.input.data = *hid_sensor->input;
      SetSensorInputDescriptor(&descriptor->sensor.input);
      descriptor->sensor.builder.set_input(fidl::unowned_ptr(&descriptor->sensor.input.descriptor));
    }
    descriptor->sensor.descriptor = descriptor->sensor.builder.build();
    descriptor->builder.set_sensor(fidl::unowned_ptr(&descriptor->sensor.descriptor));
    return ZX_OK;
  }
  if (std::holds_alternative<TouchDescriptor>(hid_desc.descriptor)) {
    const TouchDescriptor* hid_touch = &std::get<TouchDescriptor>(hid_desc.descriptor);
    if (hid_touch->input) {
      descriptor->touch.input.data = *hid_touch->input;
      SetTouchInputDescriptor(&descriptor->touch.input);
      descriptor->touch.builder.set_input(fidl::unowned_ptr(&descriptor->touch.input.descriptor));
    }
    descriptor->touch.descriptor = descriptor->touch.builder.build();
    descriptor->builder.set_touch(fidl::unowned_ptr(&descriptor->touch.descriptor));
    return ZX_OK;
  }
  if (std::holds_alternative<KeyboardDescriptor>(hid_desc.descriptor)) {
    const KeyboardDescriptor* hid_keyboard = &std::get<KeyboardDescriptor>(hid_desc.descriptor);
    if (hid_keyboard->input) {
      descriptor->keyboard.input.data = *hid_keyboard->input;
      SetKeyboardInputDescriptor(&descriptor->keyboard.input);
      descriptor->keyboard.builder.set_input(
          fidl::unowned_ptr(&descriptor->keyboard.input.descriptor));
    }
    if (hid_keyboard->output) {
      descriptor->keyboard.output.data = *hid_keyboard->output;
      SetKeyboardOutputDescriptor(&descriptor->keyboard.output);
      descriptor->keyboard.builder.set_output(
          fidl::unowned_ptr(&descriptor->keyboard.output.descriptor));
    }
    descriptor->keyboard.descriptor = descriptor->keyboard.builder.build();
    descriptor->builder.set_keyboard(fidl::unowned_ptr(&descriptor->keyboard.descriptor));
    return ZX_OK;
  }
  if (std::holds_alternative<ConsumerControlDescriptor>(hid_desc.descriptor)) {
    const ConsumerControlDescriptor* hid_consumer_control =
        &std::get<ConsumerControlDescriptor>(hid_desc.descriptor);
    if (hid_consumer_control->input) {
      descriptor->consumer_control.input.data = *hid_consumer_control->input;
      SetConsumerControlInputDescriptor(&descriptor->consumer_control.input);
      descriptor->consumer_control.builder.set_input(
          fidl::unowned_ptr(&descriptor->consumer_control.input.descriptor));
    }
    descriptor->consumer_control.descriptor = descriptor->consumer_control.builder.build();
    descriptor->builder.set_consumer_control(
        fidl::unowned_ptr(&descriptor->consumer_control.descriptor));
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SetFidlInputReport(const hid_input_report::InputReport& hid_report,
                               FidlInputReport* report) {
  if (hid_report.time) {
    report->time = *hid_report.time;
    report->builder.set_event_time(fidl::unowned_ptr(&report->time));
  }

  if (hid_report.trace_id) {
    report->trace_id = *hid_report.trace_id;
    report->builder.set_trace_id(fidl::unowned_ptr(&report->trace_id));
  }

  if (std::holds_alternative<MouseInputReport>(hid_report.report)) {
    report->report = FidlMouseInputReport();
    FidlMouseInputReport* mouse = &std::get<FidlMouseInputReport>(report->report);

    mouse->data = std::get<MouseInputReport>(hid_report.report);
    SetMouseInputReport(mouse);
    report->builder.set_mouse(fidl::unowned_ptr(&mouse->report));
    return ZX_OK;
  }
  if (std::holds_alternative<SensorInputReport>(hid_report.report)) {
    report->report = FidlSensorInputReport();
    FidlSensorInputReport* sensor = &std::get<FidlSensorInputReport>(report->report);

    sensor->data = std::get<SensorInputReport>(hid_report.report);
    SetSensorInputReport(sensor);
    report->builder.set_sensor(fidl::unowned_ptr(&sensor->report));
    return ZX_OK;
  }
  if (std::holds_alternative<TouchInputReport>(hid_report.report)) {
    report->report = FidlTouchInputReport();
    FidlTouchInputReport* touch = &std::get<FidlTouchInputReport>(report->report);

    touch->data = std::get<TouchInputReport>(hid_report.report);
    SetTouchInputReport(touch);
    report->builder.set_touch(fidl::unowned_ptr(&touch->report));
    return ZX_OK;
  }
  if (std::holds_alternative<KeyboardInputReport>(hid_report.report)) {
    report->report = FidlKeyboardInputReport();
    FidlKeyboardInputReport* keyboard = &std::get<FidlKeyboardInputReport>(report->report);

    keyboard->data = std::get<KeyboardInputReport>(hid_report.report);
    SetKeyboardInputReport(keyboard);
    report->builder.set_keyboard(fidl::unowned_ptr(&keyboard->report));
    return ZX_OK;
  }
  if (std::holds_alternative<ConsumerControlInputReport>(hid_report.report)) {
    report->report = FidlConsumerControlInputReport();
    FidlConsumerControlInputReport* consumer_control =
        &std::get<FidlConsumerControlInputReport>(report->report);

    consumer_control->data = std::get<ConsumerControlInputReport>(hid_report.report);
    SetConsumerControlInputReport(consumer_control);
    report->builder.set_consumer_control(fidl::unowned_ptr(&consumer_control->report));
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

MouseDescriptor ToMouseDescriptor(const fuchsia_input_report::MouseDescriptor& fidl_descriptor) {
  MouseDescriptor descriptor;
  if (fidl_descriptor.has_input()) {
    MouseInputDescriptor input;
    if (fidl_descriptor.input().has_movement_x()) {
      input.movement_x = fidl_descriptor.input().movement_x();
    }
    if (fidl_descriptor.input().has_movement_y()) {
      input.movement_y = fidl_descriptor.input().movement_y();
    }
    if (fidl_descriptor.input().has_position_x()) {
      input.position_x = fidl_descriptor.input().position_x();
    }
    if (fidl_descriptor.input().has_position_y()) {
      input.position_y = fidl_descriptor.input().position_y();
    }
    if (fidl_descriptor.input().has_scroll_v()) {
      input.scroll_v = fidl_descriptor.input().scroll_v();
    }
    if (fidl_descriptor.input().has_scroll_h()) {
      input.scroll_h = fidl_descriptor.input().scroll_h();
    }
    if (fidl_descriptor.input().has_buttons()) {
      input.num_buttons = fidl_descriptor.input().buttons().count();
      for (size_t i = 0; i < fidl_descriptor.input().buttons().count(); i++) {
        input.buttons[i] = fidl_descriptor.input().buttons()[i];
      }
    }
    descriptor.input = input;
  }
  return descriptor;
}

KeyboardDescriptor ToKeyboardDescriptor(
    const fuchsia_input_report::KeyboardDescriptor& fidl_descriptor) {
  KeyboardDescriptor descriptor;
  if (fidl_descriptor.has_input()) {
    KeyboardInputDescriptor input;
    if (fidl_descriptor.input().has_keys()) {
      input.num_keys = fidl_descriptor.input().keys().count();
      for (size_t i = 0; i < fidl_descriptor.input().keys().count(); i++) {
        input.keys[i] = fidl_descriptor.input().keys()[i];
      }
    }
    descriptor.input = input;
  }
  if (fidl_descriptor.has_output()) {
    KeyboardOutputDescriptor output;
    if (fidl_descriptor.output().has_leds()) {
      output.num_leds = fidl_descriptor.output().leds().count();
      for (size_t i = 0; i < fidl_descriptor.output().leds().count(); i++) {
        output.leds[i] = fidl_descriptor.output().leds()[i];
      }
    }
    descriptor.output = output;
  }
  return descriptor;
}

TouchDescriptor ToTouchDescriptor(const fuchsia_input_report::TouchDescriptor& fidl_descriptor) {
  TouchDescriptor descriptor;
  if (fidl_descriptor.has_input()) {
    TouchInputDescriptor input;
    if (fidl_descriptor.input().has_touch_type()) {
      input.touch_type = fidl_descriptor.input().touch_type();
    }
    if (fidl_descriptor.input().has_max_contacts()) {
      input.max_contacts = fidl_descriptor.input().max_contacts();
    }
    if (fidl_descriptor.input().has_buttons()) {
      input.num_buttons = fidl_descriptor.input().buttons().count();
      for (size_t i = 0; i < input.num_buttons; i++) {
        input.buttons[i] = fidl_descriptor.input().buttons()[i];
      }
    }

    if (fidl_descriptor.input().has_contacts()) {
      input.num_contacts = fidl_descriptor.input().contacts().count();
      for (size_t i = 0; i < fidl_descriptor.input().contacts().count(); i++) {
        ContactInputDescriptor contact;
        const fuchsia_input_report::ContactInputDescriptor& fidl_contact =
            fidl_descriptor.input().contacts()[i];

        if (fidl_contact.has_position_x()) {
          contact.position_x = fidl_contact.position_x();
        }
        if (fidl_contact.has_position_y()) {
          contact.position_y = fidl_contact.position_y();
        }
        if (fidl_contact.has_pressure()) {
          contact.pressure = fidl_contact.pressure();
        }
        if (fidl_contact.has_contact_width()) {
          contact.contact_width = fidl_contact.contact_width();
        }
        if (fidl_contact.has_contact_height()) {
          contact.contact_height = fidl_contact.contact_height();
        }
        input.contacts[i] = contact;
      }
    }

    descriptor.input = input;
  }
  return descriptor;
}

SensorDescriptor ToSensorDescriptor(const fuchsia_input_report::SensorDescriptor& fidl_descriptor) {
  SensorDescriptor descriptor;
  if (fidl_descriptor.has_input()) {
    SensorInputDescriptor input;
    if (fidl_descriptor.input().has_values()) {
      input.num_values = fidl_descriptor.input().values().count();
      for (size_t i = 0; i < fidl_descriptor.input().values().count(); i++) {
        input.values[i] = fidl_descriptor.input().values()[i];
      }
    }
    descriptor.input = input;
  }
  return descriptor;
}

ConsumerControlDescriptor ToConsumerControlDescriptor(
    const fuchsia_input_report::ConsumerControlDescriptor& fidl_descriptor) {
  ConsumerControlDescriptor descriptor;
  if (fidl_descriptor.has_input()) {
    ConsumerControlInputDescriptor input;
    if (fidl_descriptor.input().has_buttons()) {
      input.num_buttons = fidl_descriptor.input().buttons().count();
      for (size_t i = 0; i < fidl_descriptor.input().buttons().count(); i++) {
        input.buttons[i] = fidl_descriptor.input().buttons()[i];
      }
    }
    descriptor.input = input;
  }
  return descriptor;
}

MouseInputReport ToMouseInputReport(const fuchsia_input_report::MouseInputReport& fidl_report) {
  MouseInputReport report = {};
  if (fidl_report.has_movement_x()) {
    report.movement_x = fidl_report.movement_x();
  }
  if (fidl_report.has_movement_y()) {
    report.movement_y = fidl_report.movement_y();
  }
  if (fidl_report.has_position_x()) {
    report.position_x = fidl_report.position_x();
  }
  if (fidl_report.has_position_y()) {
    report.position_y = fidl_report.position_y();
  }
  if (fidl_report.has_scroll_v()) {
    report.scroll_v = fidl_report.scroll_v();
  }
  if (fidl_report.has_scroll_h()) {
    report.scroll_h = fidl_report.scroll_h();
  }
  if (fidl_report.has_pressed_buttons()) {
    report.num_buttons_pressed = fidl_report.pressed_buttons().count();
    for (size_t i = 0; i < fidl_report.pressed_buttons().count(); i++) {
      report.buttons_pressed[i] = fidl_report.pressed_buttons()[i];
    }
  }
  return report;
}

KeyboardInputReport ToKeyboardInputReport(
    const fuchsia_input_report::KeyboardInputReport& fidl_report) {
  KeyboardInputReport report = {};
  if (fidl_report.has_pressed_keys()) {
    report.num_pressed_keys = fidl_report.pressed_keys().count();
    for (size_t i = 0; i < fidl_report.pressed_keys().count(); i++) {
      report.pressed_keys[i] = fidl_report.pressed_keys()[i];
    }
  }
  return report;
}

TouchInputReport ToTouchInputReport(const fuchsia_input_report::TouchInputReport& fidl_report) {
  TouchInputReport report = {};
  if (fidl_report.has_contacts()) {
    report.num_contacts = fidl_report.contacts().count();
    for (size_t i = 0; i < fidl_report.contacts().count(); i++) {
      ContactInputReport contact;
      const fuchsia_input_report::ContactInputReport& fidl_contact = fidl_report.contacts()[i];

      if (fidl_contact.has_position_x()) {
        contact.position_x = fidl_contact.position_x();
      }
      if (fidl_contact.has_position_y()) {
        contact.position_y = fidl_contact.position_y();
      }
      if (fidl_contact.has_pressure()) {
        contact.pressure = fidl_contact.pressure();
      }
      if (fidl_contact.has_contact_width()) {
        contact.contact_width = fidl_contact.contact_width();
      }
      if (fidl_contact.has_contact_height()) {
        contact.contact_height = fidl_contact.contact_height();
      }
      report.contacts[i] = contact;
    }
  }
  if (fidl_report.has_pressed_buttons()) {
    report.num_pressed_buttons = fidl_report.pressed_buttons().count();
    for (size_t i = 0; i < report.num_pressed_buttons; i++) {
      report.pressed_buttons[i] = fidl_report.pressed_buttons()[i];
    }
  }
  return report;
}

SensorInputReport ToSensorInputReport(const fuchsia_input_report::SensorInputReport& fidl_report) {
  SensorInputReport report = {};
  if (fidl_report.has_values()) {
    report.num_values = fidl_report.values().count();
    for (size_t i = 0; i < fidl_report.values().count(); i++) {
      report.values[i] = fidl_report.values()[i];
    }
  }
  return report;
}

ConsumerControlInputReport ToConsumerControlInputReport(
    const fuchsia_input_report::ConsumerControlInputReport& fidl_report) {
  ConsumerControlInputReport report = {};
  if (fidl_report.has_pressed_buttons()) {
    report.num_pressed_buttons = fidl_report.pressed_buttons().count();
    for (size_t i = 0; i < fidl_report.pressed_buttons().count(); i++) {
      report.pressed_buttons[i] = fidl_report.pressed_buttons()[i];
    }
  }
  return report;
}

InputReport ToInputReport(const fuchsia_input_report::InputReport& fidl_report) {
  InputReport report = {};

  if (fidl_report.has_event_time()) {
    report.time = fidl_report.event_time();
  }

  if (fidl_report.has_mouse()) {
    report.report = ToMouseInputReport(fidl_report.mouse());
  } else if (fidl_report.has_keyboard()) {
    report.report = ToKeyboardInputReport(fidl_report.keyboard());
  } else if (fidl_report.has_touch()) {
    report.report = ToTouchInputReport(fidl_report.touch());
  } else if (fidl_report.has_sensor()) {
    report.report = ToSensorInputReport(fidl_report.sensor());
  } else if (fidl_report.has_consumer_control()) {
    report.report = ToConsumerControlInputReport(fidl_report.consumer_control());
  }
  return report;
}

}  // namespace hid_input_report
