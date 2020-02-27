// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/device.h"

#include "src/ui/input/lib/hid-input-report/consumer_control.h"
#include "src/ui/input/lib/hid-input-report/keyboard.h"
#include "src/ui/input/lib/hid-input-report/mouse.h"
#include "src/ui/input/lib/hid-input-report/sensor.h"
#include "src/ui/input/lib/hid-input-report/touch.h"

namespace hid_input_report {

ParseResult CreateDevice(const hid::ReportDescriptor* descriptor,
                         std::unique_ptr<Device>* out_device) {
  // Traverse up the nested collections to the Application collection.
  hid::Collection* collection = descriptor->input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    return ParseResult::kNoCollection;
  }

  std::unique_ptr<hid_input_report::Device> parse_device;

  uint16_t page = collection->usage.page;
  uint32_t usage = collection->usage.usage;
  if ((page == ::hid::usage::Page::kGenericDesktop) &&
      (usage == ::hid::usage::GenericDesktop::kMouse)) {
    parse_device = std::make_unique<hid_input_report::Mouse>();
  } else if (page == ::hid::usage::Page::kSensor) {
    parse_device = std::make_unique<hid_input_report::Sensor>();
  } else if (page == ::hid::usage::Page::kDigitizer &&
             (usage == ::hid::usage::Digitizer::kTouchScreen)) {
    parse_device = std::make_unique<hid_input_report::Touch>();
  } else if (page == ::hid::usage::Page::kDigitizer &&
             (usage == ::hid::usage::Digitizer::kTouchPad)) {
    parse_device = std::make_unique<hid_input_report::Touch>();
  } else if (page == ::hid::usage::Page::kGenericDesktop &&
             (usage == ::hid::usage::GenericDesktop::kKeyboard)) {
    parse_device = std::make_unique<hid_input_report::Keyboard>();
  } else if (page == ::hid::usage::Page::kConsumer &&
             (usage == ::hid::usage::Consumer::kConsumerControl)) {
    parse_device = std::make_unique<hid_input_report::ConsumerControl>();
  }

  if (!parse_device) {
    return ParseResult::kItemNotFound;
  }

  ParseResult result = parse_device->ParseReportDescriptor(*descriptor);
  if (result == hid_input_report::ParseResult::kOk) {
    *out_device = std::move(parse_device);
  }
  return result;
}

}  // namespace hid_input_report
