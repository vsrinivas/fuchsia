// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_REPORT_H_
#define APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_REPORT_H_

#include <stdint.h>

#include <vector>

#include "apps/mozart/src/launcher/input/input_descriptor.h"
#include "lib/ftl/time/time_point.h"

namespace mozart {
namespace input {

struct InputReport {
  enum ReportType {
    kKeyboard,
    kMouse,
    kStylus,
    kTouchscreen,
  };

  ftl::TimePoint timestamp;
};

struct KeyboardReport : InputReport {
  std::vector<KeyUsage> down;
};

struct MouseReport : InputReport {
  int32_t rel_x = 0;
  int32_t rel_y = 0;
  int32_t hscroll = 0;
  int32_t vscroll = 0;
  uint32_t buttons = 0;
};

struct StylusReport : InputReport {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t pressure = 0;
  bool in_range = false;
  bool is_down = false;
  std::vector<ButtonUsage> down;
};

struct Touch {
  int32_t finger_id = 0;
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct TouchReport : InputReport {
  std::vector<Touch> touches;
};
}
}

#endif  // APPS_MOZART_SRC_LAUNCHER_INPUT_INPUT_REPORT_H_
