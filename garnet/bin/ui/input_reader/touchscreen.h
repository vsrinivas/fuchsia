// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_
#define GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_

#include "garnet/bin/ui/input_reader/device.h"
#include "garnet/bin/ui/input_reader/touch.h"

#include <cstddef>

#include <hid-parser/parser.h>

namespace mozart {
class TouchScreen : public Device {
 public:
  // |Device|
  bool ParseReportDescriptor(const hid::ReportDescriptor& report_descriptor,
                             Device::Descriptor* device_descriptor) override;
  // |Device|
  bool ParseReport(const uint8_t* data, size_t len,
                   fuchsia::ui::input::InputReport* report) override;
  // |Device|
  uint8_t ReportId() const override { return touch_.ReportId(); }

 private:
  Touch touch_ = {};
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_
