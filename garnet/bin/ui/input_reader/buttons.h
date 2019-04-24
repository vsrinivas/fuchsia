// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_BUTTONS_H_
#define GARNET_BIN_UI_INPUT_READER_BUTTONS_H_

#include <hid-parser/parser.h>

#include <cstddef>

#include "garnet/bin/ui/input_reader/device.h"
#include "garnet/bin/ui/input_reader/touch.h"

namespace ui_input {
class Buttons : public Device {
 public:
  // |Device|
  bool ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                             Device::Descriptor* device_descriptor) override;
  // |Device|
  bool ParseReport(const uint8_t* data, size_t len,
                   fuchsia::ui::input::InputReport* report) override;
  // |Device|
  uint8_t ReportId() const override { return report_id_; }

 private:
  enum Capabilities : uint32_t {
    VOLUME = 1 << 0,
    PHONE_MUTE = 1 << 1,
  };
  uint32_t capabilities_ = 0;

  hid::Attributes volume_ = {};
  hid::Attributes phone_mute_ = {};

  size_t report_size_ = 0;
  uint8_t report_id_ = 0;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_BUTTONS_H_
