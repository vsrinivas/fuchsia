// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_
#define GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_

#include <cstddef>

#include <hid-parser/parser.h>

namespace mozart {

class Touchscreen {
 public:
  static constexpr size_t MAX_TOUCH_POINTS = 10;

  enum Capabilities : uint32_t {
    CONTACT_ID = 1 << 0,
    TIP_SWITCH = 1 << 1,
    X = 1 << 2,
    Y = 1 << 3,
    SCAN_TIME = 1 << 16,
    CONTACT_COUNT = 1 << 17,
  };

  struct ContactReport {
    uint32_t id;
    // x and y are unitless. The touchscreen descriptor declares a logical max
    // and min as well as a resolution, and this describes how the x and y will
    // be used.
    int32_t x;
    int32_t y;
  };

  struct Report {
    // Scan time currently is in whatever seconds unit that the report
    // descriptor defines
    // TODO(ZX-3287) Convert scan time to microseconds
    uint32_t scan_time;
    size_t contact_count;
    ContactReport contacts[MAX_TOUCH_POINTS];
  };

  struct Descriptor {
    int32_t x_logical_min;
    int32_t x_logical_max;
    int32_t x_resolution;

    int32_t y_logical_min;
    int32_t y_logical_max;
    int32_t y_resolution;

    int32_t max_finger_id;
  };

  Touchscreen()
      : touch_points_(0),
        scan_time_(),
        contact_count_(),
        capabilities_(0),
        report_size_(0) {
    for (size_t i = 0; i < MAX_TOUCH_POINTS; i++) {
      configs_[i] = {};
    }
  }

  bool ParseTouchscreenDescriptor(const hid::ReportDescriptor *desc);
  bool ParseReport(const uint8_t *data, size_t len, Report *report) const;

  uint8_t report_id() const { return report_id_; }
  size_t touch_points() const { return touch_points_; }
  int32_t contact_id_max() const { return contact_id_max_; }
  uint32_t capabilities() const { return capabilities_; }
  int32_t x_logical_min() const { return configs_[0].x.logc_mm.min; }
  int32_t x_logical_max() const { return configs_[0].x.logc_mm.max; }
  int32_t y_logical_min() const { return configs_[0].y.logc_mm.min; }
  int32_t y_logical_max() const { return configs_[0].y.logc_mm.max; }

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
  uint32_t capabilities_;
  size_t report_size_;
  uint8_t report_id_;
  int32_t contact_id_max_;
};
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_TOUCHSCREEN_H_
