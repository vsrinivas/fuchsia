// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/touch.h"

#include <stdint.h>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {

ParseResult Touch::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  ContactConfig contacts[fuchsia_input_report::TOUCH_MAX_CONTACTS];
  size_t num_contacts = 0;
  hid::Attributes buttons[fuchsia_input_report::TOUCH_MAX_NUM_BUTTONS];
  uint8_t num_buttons = 0;

  // Traverse up the nested collections to the Application collection.
  hid::Collection* main_collection = hid_report_descriptor.input_fields[0].col;
  while (main_collection != nullptr) {
    if (main_collection->type == hid::CollectionType::kApplication) {
      break;
    }
    main_collection = main_collection->parent;
  }
  if (!main_collection) {
    return ParseResult::kNoCollection;
  }

  if (main_collection->usage ==
      hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchScreen)) {
    touch_type_ = fuchsia_input_report::TouchType::TOUCHSCREEN;
  } else if (main_collection->usage ==
             hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchPad)) {
    touch_type_ = fuchsia_input_report::TouchType::TOUCHPAD;
  } else {
    return ParseResult::kNoCollection;
  }

  hid::Collection* finger_collection = nullptr;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField field = hid_report_descriptor.input_fields[i];

    // Process the global items.
    if (field.attr.usage.page == hid::usage::Page::kButton) {
      if (num_buttons == fuchsia_input_report::TOUCH_MAX_NUM_BUTTONS) {
        return ParseResult::kTooManyItems;
      }
      buttons[num_buttons] = field.attr;
      num_buttons++;
    }

    // Process touch points. Don't process the item if it's not part of a touch point collection.
    if (field.col->usage !=
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kFinger)) {
      continue;
    }

    // If our collection pointer is different than the previous collection
    // pointer, we have started a new collection and are on a new touch point
    if (field.col != finger_collection) {
      finger_collection = field.col;
      num_contacts++;
    }

    if (num_contacts < 1) {
      return ParseResult::kNoCollection;
    }
    if (num_contacts > fuchsia_input_report::TOUCH_MAX_CONTACTS) {
      return ParseResult::kTooManyItems;
    }
    ContactConfig* contact = &contacts[num_contacts - 1];

    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID)) {
      contact->contact_id = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTipSwitch)) {
      contact->tip_switch = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kX)) {
      contact->position_x = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kY)) {
      contact->position_y = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTipPressure)) {
      contact->pressure = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kWidth)) {
      contact->contact_width = field.attr;
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kHeight)) {
      contact->contact_height = field.attr;
    }
  }

  // No error, write to class members.
  for (size_t i = 0; i < num_contacts; i++) {
    contacts_[i] = contacts[i];
  }
  num_contacts_ = num_contacts;
  for (size_t i = 0; i < num_buttons; i++) {
    buttons_[i] = buttons[i];
  }
  num_buttons_ = num_buttons;

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult Touch::CreateDescriptor(fidl::Allocator* allocator,
                                    fuchsia_input_report::DeviceDescriptor::Builder* descriptor) {
  auto input = fuchsia_input_report::TouchInputDescriptor::Builder(
      allocator->make<fuchsia_input_report::TouchInputDescriptor::Frame>());

  input.set_touch_type(allocator->make<fuchsia_input_report::TouchType>(touch_type_));

  auto input_contacts =
      allocator->make<fuchsia_input_report::ContactInputDescriptor[]>(num_contacts_);

  for (size_t i = 0; i < num_contacts_; i++) {
    const ContactConfig& config = contacts_[i];
    auto contact = fuchsia_input_report::ContactInputDescriptor::Builder(
        allocator->make<fuchsia_input_report::ContactInputDescriptor::Frame>());

    if (config.position_x) {
      contact.set_position_x(
          allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*config.position_x)));
    }
    if (config.position_y) {
      contact.set_position_y(
          allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*config.position_y)));
    }
    if (config.pressure) {
      contact.set_pressure(
          allocator->make<fuchsia_input_report::Axis>(LlcppAxisFromAttribute(*config.pressure)));
    }
    if (config.contact_width) {
      contact.set_contact_width(allocator->make<fuchsia_input_report::Axis>(
          LlcppAxisFromAttribute(*config.contact_width)));
    }
    if (config.contact_height) {
      contact.set_contact_height(allocator->make<fuchsia_input_report::Axis>(
          LlcppAxisFromAttribute(*config.contact_height)));
    }

    input_contacts[i] = contact.build();
  }

  input.set_contacts(
      allocator->make<fidl::VectorView<fuchsia_input_report::ContactInputDescriptor>>(
          std::move(input_contacts), num_contacts_));

  // Set the buttons array.
  {
    auto buttons = allocator->make<uint8_t[]>(num_buttons_);
    size_t index = 0;
    for (auto& button : buttons_) {
      buttons[index++] = button.usage.usage;
    }
    auto buttons_view =
        allocator->make<fidl::VectorView<uint8_t>>(std::move(buttons), num_buttons_);
    input.set_buttons(std::move(buttons_view));
  }

  auto touch = fuchsia_input_report::TouchDescriptor::Builder(
      allocator->make<fuchsia_input_report::TouchDescriptor::Frame>());
  touch.set_input(allocator->make<fuchsia_input_report::TouchInputDescriptor>(input.build()));
  descriptor->set_touch(allocator->make<fuchsia_input_report::TouchDescriptor>(touch.build()));

  return ParseResult::kOk;
}

ParseResult Touch::ParseInputReport(const uint8_t* data, size_t len, fidl::Allocator* allocator,
                                    fuchsia_input_report::InputReport::Builder* report) {
  if (len != report_size_) {
    return ParseResult::kReportSizeMismatch;
  }
  auto touch = fuchsia_input_report::TouchInputReport::Builder(
      allocator->make<fuchsia_input_report::TouchInputReport::Frame>());

  // Calculate the number of active contacts.
  size_t num_active_contacts = 0;
  for (size_t i = 0; i < num_contacts_; i++) {
    if (!contacts_[i].tip_switch) {
      num_active_contacts = num_contacts_;
      break;
    }
    double val_out_double;
    if (!ExtractAsUnitType(data, len, *contacts_[i].tip_switch, &val_out_double)) {
      continue;
    }
    if (static_cast<uint32_t>(val_out_double) != 0) {
      num_active_contacts++;
    }
  }

  auto input_contacts = allocator->make<fuchsia_input_report::ContactInputReport[]>(num_contacts_);

  size_t contact_index = 0;
  for (size_t i = 0; i < num_contacts_; i++) {
    double val_out;

    if (contacts_[i].tip_switch) {
      if (ExtractAsUnitType(data, len, *contacts_[i].tip_switch, &val_out)) {
        if (static_cast<uint32_t>(val_out) == 0) {
          continue;
        }
      }
    }

    auto contact = fuchsia_input_report::ContactInputReport::Builder(
        allocator->make<fuchsia_input_report::ContactInputReport::Frame>());

    if (contacts_[i].contact_id) {
      // Some touchscreens we support mistakenly set the logical range to 0-1 for the
      // tip switch and then never reset the range for the contact id. For this reason,
      // we have to do an "unconverted" extraction.
      uint32_t contact_id;
      if (hid::ExtractUint(data, len, *contacts_[i].contact_id, &contact_id)) {
        contact.set_contact_id(allocator->make<uint32_t>(contact_id));
      }
    }

    if (contacts_[i].position_x) {
      contact.set_position_x(Extract<int64_t>(data, len, *contacts_[i].position_x, allocator));
    }
    if (contacts_[i].position_y) {
      contact.set_position_y(Extract<int64_t>(data, len, *contacts_[i].position_y, allocator));
    }
    if (contacts_[i].pressure) {
      contact.set_pressure(Extract<int64_t>(data, len, *contacts_[i].pressure, allocator));
    }
    if (contacts_[i].contact_width) {
      contact.set_contact_width(
          Extract<int64_t>(data, len, *contacts_[i].contact_width, allocator));
    }
    if (contacts_[i].contact_height) {
      contact.set_contact_height(
          Extract<int64_t>(data, len, *contacts_[i].contact_height, allocator));
    }

    input_contacts[contact_index++] = contact.build();
  }

  touch.set_contacts(allocator->make<fidl::VectorView<fuchsia_input_report::ContactInputReport>>(
      std::move(input_contacts), num_active_contacts));

  // Parse Buttons.
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
  touch.set_pressed_buttons(
      allocator->make<fidl::VectorView<uint8_t>>(std::move(fidl_buttons), buttons_size));

  report->set_touch(allocator->make<fuchsia_input_report::TouchInputReport>(touch.build()));

  return ParseResult::kOk;
}

}  // namespace hid_input_report
