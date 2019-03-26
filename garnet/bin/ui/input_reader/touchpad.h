// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TOUCHPAD_H_
#define GARNET_BIN_UI_INPUT_READER_TOUCHPAD_H_

#include "garnet/bin/ui/input_reader/device.h"
#include "garnet/bin/ui/input_reader/touch.h"

#include <cstddef>

#include <hid-parser/parser.h>

namespace mozart {

// This represents a HID touchpad device. It currently converts Touch
// information into a Mouse InputReport.
class Touchpad : public Device {
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
  bool ParseTouchpadReport(const Touch::Report& touchpad,
                           fuchsia::ui::input::InputReport* report);
  Touch touch_ = {};

  // These variables do conversion from touchpad information
  // into mouse information. All information is from the previous seen report,
  // which enables us to do relative deltas and finger tracking.

  // True if any fingers are pressed on the touchpad.
  bool has_touch_ = false;
  // True if the tracking finger is no longer pressed, but other fingers are
  // still pressed.
  bool tracking_finger_was_lifted_ = false;
  // Used to keep track of which finger is controlling the mouse on a touchpad
  uint32_t tracking_finger_id_ = 0;
  // Used for converting absolute coords from touchpad into relative deltas
  int32_t mouse_abs_x_ = -1;
  int32_t mouse_abs_y_ = -1;
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_TOUCHPAD_H_
