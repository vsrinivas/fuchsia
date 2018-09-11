// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_TESTS_UTIL_H_
#define GARNET_LIB_UI_INPUT_TESTS_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "garnet/lib/ui/gfx/id.h"

namespace lib_ui_input_tests {

// Creates pointer events for one finger, where the pointer "device" is tied to
// one compositor. Helps remove boilerplate clutter.
//
// N.B. It's easy to create an event stream with inconsistent state, e.g.,
// sending ADD ADD.  Client is responsible for ensuring desired usage.
class PointerEventGenerator {
 public:
  PointerEventGenerator(scenic::ResourceId compositor_id, uint32_t device_id,
                        uint32_t pointer_id,
                        fuchsia::ui::input::PointerEventType type);
  ~PointerEventGenerator() = default;

  fuchsia::ui::input::Command Add(float x, float y);
  fuchsia::ui::input::Command Down(float x, float y);
  fuchsia::ui::input::Command Move(float x, float y);
  fuchsia::ui::input::Command Up(float x, float y);
  fuchsia::ui::input::Command Remove(float x, float y);

 private:
  fuchsia::ui::input::Command MakeInputCommand(
      fuchsia::ui::input::PointerEvent event);

  scenic::ResourceId compositor_id_;
  fuchsia::ui::input::PointerEvent blank_;
};

}  // namespace lib_ui_input_tests

#endif  // GARNET_LIB_UI_INPUT_TESTS_UTIL_H_
