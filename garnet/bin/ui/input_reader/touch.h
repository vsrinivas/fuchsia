// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TOUCH_H_
#define GARNET_BIN_UI_INPUT_READER_TOUCH_H_

#include <hid-parser/parser.h>

#include <cstddef>

namespace ui_input {

// This reprents a HID device that uses touch. It is a helper class that both
// touchscreens and touchpads rely on.
class Touch {
 public:
  static constexpr size_t MAX_TOUCH_POINTS = 10;

  enum Capabilities : uint32_t {
    CONTACT_ID = 1 << 0,
    TIP_SWITCH = 1 << 1,
    X = 1 << 2,
    Y = 1 << 3,
    SCAN_TIME = 1 << 16,
    CONTACT_COUNT = 1 << 17,
    BUTTON = 1 << 18,
  };

  struct ContactReport {
    uint32_t id;
    // x and y are have units of 10 microns (10^-5 meters). This seems to give
    // the most precision without having the values overflow. If the report
    // descriptor does not define units, the value will be passed on
    // unconverted.
    int32_t x;
    int32_t y;
  };

  struct Report {
    // Scan time in microseconds. If the report descriptor does not
    // define units, the value will be passed on unconverted.
    uint32_t scan_time;
    size_t contact_count;
    bool button;
    ContactReport contacts[MAX_TOUCH_POINTS];
  };

  struct Descriptor {
    // The min and max of x and y have units of 10^-5 meters. If the
    // report descriptor does not define units, the value will be passed
    // on unconverted.
    int32_t x_min;
    int32_t x_max;
    int32_t x_resolution;

    int32_t y_min;
    int32_t y_max;
    int32_t y_resolution;

    int32_t max_finger_id;
  };

  Touch()
      : touch_points_(0),
        scan_time_(),
        contact_count_(),
        capabilities_(0),
        report_size_(0) {
    for (size_t i = 0; i < MAX_TOUCH_POINTS; i++) {
      configs_[i] = {};
    }
  }

  bool ParseTouchDescriptor(const hid::ReportDescriptor &desc);
  bool ParseReport(const uint8_t *data, size_t len, Report *report) const;
  bool SetDescriptor(Touch::Descriptor *touch_desc);
  uint8_t ReportId() const { return report_id_; }

  size_t touch_points() const { return touch_points_; }
  int32_t contact_id_max() const { return contact_id_max_; }
  uint32_t capabilities() const { return capabilities_; }

 private:
  struct TouchPointConfig {
    uint32_t capabilities;
    hid::Attributes contact_id;
    hid::Attributes tip_switch;
    hid::Attributes x;
    hid::Attributes y;
  };

  size_t touch_points_;
  TouchPointConfig configs_[MAX_TOUCH_POINTS];
  hid::Attributes scan_time_;
  hid::Attributes contact_count_;
  hid::Attributes button_;
  uint32_t capabilities_;
  size_t report_size_;
  uint8_t report_id_;
  int32_t contact_id_max_;
};
}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_TOUCH_H_
