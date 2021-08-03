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
  hid::Attributes buttons[fuchsia_input_report::wire::kMouseMaxNumButtons];
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
      if (num_buttons == fuchsia_input_report::wire::kMouseMaxNumButtons) {
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

ParseResult Mouse::CreateDescriptor(fidl::AnyArena& allocator,
                                    fuchsia_input_report::wire::DeviceDescriptor& descriptor) {
  fuchsia_input_report::wire::MouseInputDescriptor mouse_input(allocator);

  if (movement_x_) {
    mouse_input.set_movement_x(allocator, LlcppAxisFromAttribute(*movement_x_));
  }

  if (movement_y_) {
    mouse_input.set_movement_y(allocator, LlcppAxisFromAttribute(*movement_y_));
  }

  if (position_x_) {
    mouse_input.set_position_x(allocator, LlcppAxisFromAttribute(*position_x_));
  }

  if (position_y_) {
    mouse_input.set_position_y(allocator, LlcppAxisFromAttribute(*position_y_));
  }

  if (scroll_v_) {
    mouse_input.set_scroll_v(allocator, LlcppAxisFromAttribute(*scroll_v_));
  }

  // Set the buttons array.
  {
    fidl::VectorView<uint8_t> buttons(allocator, num_buttons_);
    size_t index = 0;
    for (auto& button : buttons_) {
      buttons[index++] = button.usage.usage;
    }
    mouse_input.set_buttons(allocator, std::move(buttons));
  }

  fuchsia_input_report::wire::MouseDescriptor mouse(allocator);
  mouse.set_input(allocator, std::move(mouse_input));
  descriptor.set_mouse(allocator, std::move(mouse));

  return ParseResult::kOk;
}

ParseResult Mouse::ParseInputReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                                    fuchsia_input_report::wire::InputReport& input_report) {
  if (len != report_size_) {
    return ParseResult::kReportSizeMismatch;
  }

  fuchsia_input_report::wire::MouseInputReport mouse_report(allocator);

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

  std::array<uint8_t, fuchsia_input_report::wire::kMouseMaxNumButtons> buttons;
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

  fidl::VectorView<uint8_t> fidl_buttons(allocator, buttons_size);
  for (size_t i = 0; i < buttons_size; i++) {
    fidl_buttons[i] = buttons[i];
  }
  mouse_report.set_pressed_buttons(allocator, std::move(fidl_buttons));

  input_report.set_mouse(allocator, std::move(mouse_report));
  return ParseResult::kOk;
}

}  // namespace hid_input_report
