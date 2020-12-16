// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_INSPECT_H_
#define SRC_UI_BIN_ROOT_PRESENTER_INSPECT_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

#include <src/lib/fxl/macros.h>

namespace root_presenter {

class InputReportInspector {
 public:
  explicit InputReportInspector(inspect::Node node);

  void OnInputReport(const fuchsia::ui::input::InputReport& report);

 private:
  inspect::Node node_;

  // Note: keyboard, mouse, stylus, and sensor reports also exist, but are excluded as they're
  // unused.
  inspect::ExponentialUintHistogram media_buttons_;
  inspect::ExponentialUintHistogram touchscreen_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputReportInspector);
};

struct InputEventInspector {
 public:
  explicit InputEventInspector(inspect::Node node);

  void OnInputEvent(const fuchsia::ui::input::InputEvent& event);

 private:
  inspect::Node node_;

  // Note: keyboard and focus events also exist, but are excluded as they're unused at the
  // root-presenter level.
  inspect::ExponentialUintHistogram pointer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputEventInspector);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_INSPECT_H_
