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
  TouchInputDescriptor descriptor = {};

  // Traverse up the nested collections to the Application collection.
  hid::Collection* main_collection = hid_report_descriptor.input_fields[0].col;
  while (main_collection != nullptr) {
    if (main_collection->type == hid::CollectionType::kApplication) {
      break;
    }
    main_collection = main_collection->parent;
  }
  if (!main_collection) {
    return kParseNoCollection;
  }

  if (main_collection->usage ==
      hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchScreen)) {
    descriptor.touch_type = fuchsia_input_report::TouchType::TOUCHSCREEN;
  } else if (main_collection->usage ==
             hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchPad)) {
    descriptor.touch_type = fuchsia_input_report::TouchType::TOUCHPAD;
  } else {
    return ParseResult::kParseNoCollection;
  }

  hid::Collection* finger_collection = nullptr;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField field = hid_report_descriptor.input_fields[i];

    // Process the global items.
    if (field.attr.usage.page == hid::usage::Page::kButton) {
      if (num_buttons == fuchsia_input_report::TOUCH_MAX_NUM_BUTTONS) {
        return kParseTooManyItems;
      }
      buttons[num_buttons] = field.attr;
      descriptor.buttons[num_buttons] = static_cast<uint8_t>(field.attr.usage.usage);
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
      return ParseResult::kParseNoCollection;
    }
    if (num_contacts > fuchsia_input_report::TOUCH_MAX_CONTACTS) {
      return kParseTooManyItems;
    }
    ContactConfig* contact = &contacts[num_contacts - 1];
    ContactInputDescriptor* contact_descriptor = &descriptor.contacts[num_contacts - 1];

    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID)) {
      contact->contact_id = field.attr;
      contact_descriptor->contact_id = LlcppAxisFromAttribute(contact->contact_id);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTipSwitch)) {
      contact->tip_switch = field.attr;
      contact_descriptor->is_pressed = LlcppAxisFromAttribute(contact->tip_switch);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kX)) {
      contact->position_x = field.attr;
      contact_descriptor->position_x = LlcppAxisFromAttribute(contact->position_x);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kY)) {
      contact->position_y = field.attr;
      contact_descriptor->position_y = LlcppAxisFromAttribute(contact->position_y);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTipPressure)) {
      contact->pressure = field.attr;
      contact_descriptor->pressure = LlcppAxisFromAttribute(contact->pressure);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kWidth)) {
      contact->contact_width = field.attr;
      contact_descriptor->contact_width = LlcppAxisFromAttribute(contact->contact_width);
    }
    if (field.attr.usage ==
        hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kHeight)) {
      contact->contact_height = field.attr;
      contact_descriptor->contact_height = LlcppAxisFromAttribute(contact->contact_height);
    }
  }

  // No error, write to class members.
  for (size_t i = 0; i < num_contacts; i++) {
    contacts_[i] = contacts[i];
  }
  for (size_t i = 0; i < num_buttons; i++) {
    buttons_[i] = buttons[i];
  }
  num_buttons_ = num_buttons;
  descriptor.num_buttons = num_buttons;

  descriptor.max_contacts = static_cast<uint32_t>(num_contacts);
  descriptor.num_contacts = num_contacts;
  descriptor_.input = descriptor;

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return kParseOk;
}

ReportDescriptor Touch::GetDescriptor() {
  ReportDescriptor report_descriptor = {};
  report_descriptor.descriptor = descriptor_;
  return report_descriptor;
}

ParseResult Touch::ParseInputReport(const uint8_t* data, size_t len, InputReport* report) {
  TouchInputReport touch_report = {};
  if (len != report_size_) {
    return kParseReportSizeMismatch;
  }

  double value_out;

  // Extract the global items.
  for (size_t i = 0; i < num_buttons_; i++) {
    if (hid::ExtractAsUnitType(data, len, buttons_[i], &value_out)) {
      uint8_t pressed = (value_out > 0) ? 1 : 0;
      if (pressed) {
        touch_report.pressed_buttons[touch_report.num_pressed_buttons] =
            static_cast<uint8_t>(buttons_[i].usage.usage);
        touch_report.num_pressed_buttons++;
      }
    }
  }

  // Extract each touch item.
  size_t contact_num = 0;
  for (size_t i = 0; i < descriptor_.input->num_contacts; i++) {
    ContactInputReport& contact = touch_report.contacts[contact_num];
    if (descriptor_.input->contacts[i].is_pressed) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].tip_switch, &value_out)) {
        contact.is_pressed = static_cast<bool>(value_out);
        if (!*contact.is_pressed) {
          continue;
        }
      }
    }
    contact_num++;
    if (descriptor_.input->contacts[i].contact_id) {
      // Some touchscreens we support mistakenly set the logical range to 0-1 for the
      // tip switch and then never reset the range for the contact id. For this reason,
      // we have to do an "unconverted" extraction.
      uint32_t contact_id;
      if (hid::ExtractUint(data, len, contacts_[i].contact_id, &contact_id)) {
        contact.contact_id = contact_id;
      }
    }
    if (descriptor_.input->contacts[i].position_x) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].position_x, &value_out)) {
        contact.position_x = static_cast<int64_t>(value_out);
      }
    }
    if (descriptor_.input->contacts[i].position_y) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].position_y, &value_out)) {
        contact.position_y = static_cast<int64_t>(value_out);
      }
    }
    if (descriptor_.input->contacts[i].pressure) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].pressure, &value_out)) {
        contact.pressure = static_cast<int64_t>(value_out);
      }
    }
    if (descriptor_.input->contacts[i].contact_width) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].contact_width, &value_out)) {
        contact.contact_width = static_cast<int64_t>(value_out);
      }
    }
    if (descriptor_.input->contacts[i].contact_height) {
      if (hid::ExtractAsUnitType(data, len, contacts_[i].contact_height, &value_out)) {
        contact.contact_height = static_cast<int64_t>(value_out);
      }
    }
  }
  touch_report.num_contacts = contact_num;

  // Now that we can't fail, set the real report.
  report->report = touch_report;

  return kParseOk;
}

}  // namespace hid_input_report
