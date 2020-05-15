// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/mouse.h"

#include <stdint.h>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {

ParseResult Mouse::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  std::optional<hid::Attributes> movement_x;
  std::optional<hid::Attributes> movement_y;
  std::optional<hid::Attributes> position_x;
  std::optional<hid::Attributes> position_y;
  std::optional<hid::Attributes> scroll_v;
  hid::Attributes buttons[fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS];
  uint8_t num_buttons = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kX)) {
      if (field.flags & hid::FieldTypeFlags::kAbsolute) {
        position_x = field.attr;
      } else {
        movement_x = field.attr;
      }
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kY)) {
      if (field.flags & hid::FieldTypeFlags::kAbsolute) {
        position_y = field.attr;
      } else {
        movement_y = field.attr;
      }
    } else if (field.attr.usage ==
               hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kWheel)) {
      scroll_v = field.attr;
    } else if (field.attr.usage.page == hid::usage::Page::kButton) {
      if (num_buttons == fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS) {
        return ParseResult::kTooManyItems;
      }
      buttons[num_buttons++] = field.attr;
    }
  }

  // No error, write to class members.
  if (movement_x) {
    movement_x_ = movement_x;
  }
  if (movement_y) {
    movement_y_ = movement_y;
  }
  if (position_x) {
    position_x_ = position_x;
  }
  if (position_y) {
    position_y_ = position_y;
  }
  if (scroll_v) {
    scroll_v_ = scroll_v;
  }
  for (size_t i = 0; i < num_buttons; i++) {
    buttons_[i] = buttons[i];
  }
  num_buttons_ = num_buttons;

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult Mouse::CreateDescriptor(fidl::Allocator* allocator,
                                    fuchsia_input_report::DeviceDescriptor::Builder* descriptor) {
  auto mouse_input = fuchsia_input_report::MouseInputDescriptor::Builder(
      allocator->make<fuchsia_input_report::MouseInputDescriptor::Frame>());

  if (movement_x_) {
    mouse_input.set_movement_x(
        allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*movement_x_)));
  }

  if (movement_y_) {
    mouse_input.set_movement_y(
        allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*movement_y_)));
  }

  if (position_x_) {
    mouse_input.set_position_x(
        allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*position_x_)));
  }

  if (position_y_) {
    mouse_input.set_position_y(
        allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*position_y_)));
  }

  if (scroll_v_) {
    mouse_input.set_scroll_v(
        allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*scroll_v_)));
  }

  // Set the buttons array.
  {
    auto buttons = allocator->make<uint8_t[]>(num_buttons_);
    size_t index = 0;
    for (auto& button : buttons_) {
      buttons[index++] = button.usage.usage;
    }
    auto buttons_view =
        allocator->make<fidl::VectorView<uint8_t>>(std::move(buttons), num_buttons_);
    mouse_input.set_buttons(std::move(buttons_view));
  }

  auto mouse = fuchsia_input_report::MouseDescriptor::Builder(
      allocator->make<fuchsia_input_report::MouseDescriptor::Frame>());
  mouse.set_input(allocator->make<fuchsia_input_report::MouseInputDescriptor>(mouse_input.build()));
  descriptor->set_mouse(allocator->make<fuchsia_input_report::MouseDescriptor>(mouse.build()));

  return ParseResult::kOk;
}

ParseResult Mouse::ParseInputReport(const uint8_t* data, size_t len, fidl::Allocator* allocator,
                                    fuchsia_input_report::InputReport::Builder* report) {
  if (len != report_size_) {
    return ParseResult::kReportSizeMismatch;
  }

  auto mouse_report = fuchsia_input_report::MouseInputReport::Builder(
      allocator->make<fuchsia_input_report::MouseInputReport::Frame>());

  if (movement_x_) {
    mouse_report.set_movement_x(Extract<int64_t>(data, len, *movement_x_, allocator));
  }
  if (movement_y_) {
    mouse_report.set_movement_y(Extract<int64_t>(data, len, *movement_y_, allocator));
  }
  if (position_x_) {
    mouse_report.set_position_x(Extract<int64_t>(data, len, *position_x_, allocator));
  }
  if (position_y_) {
    mouse_report.set_position_y(Extract<int64_t>(data, len, *position_y_, allocator));
  }
  if (scroll_v_) {
    mouse_report.set_scroll_v(Extract<int64_t>(data, len, *scroll_v_, allocator));
  }

  std::array<uint8_t, fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS> buttons;
  size_t buttons_size = 0;
  for (size_t i = 0; i < num_buttons_; i++) {
    double value_out;
    if (hid::ExtractAsUnitType(data, len, buttons_[i], &value_out)) {
      uint8_t pressed = (value_out > 0) ? 1 : 0;
      if (pressed) {
        buttons[buttons_size++] = static_cast<uint8_t>(buttons_[i].usage.usage);
      }
    }
  }

  auto fidl_buttons = allocator->make<uint8_t[]>(buttons_size);
  for (size_t i = 0; i < buttons_size; i++) {
    fidl_buttons[i] = buttons[i];
  }
  mouse_report.set_pressed_buttons(
      allocator->make<fidl::VectorView<uint8_t>>(std::move(fidl_buttons), buttons_size));

  report->set_mouse(allocator->make<fuchsia_input_report::MouseInputReport>(mouse_report.build()));
  return ParseResult::kOk;
}

}  // namespace hid_input_report
