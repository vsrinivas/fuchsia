// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_INTEGRATION_TESTS_UTILS_H_
#define SRC_UI_SCENIC_INTEGRATION_TESTS_UTILS_H_

#include <fuchsia/ui/input/cpp/fidl.h>

namespace integration_tests {

bool PointerMatches(
    const fuchsia::ui::input::PointerEvent& event, uint32_t pointer_id,
    fuchsia::ui::input::PointerEventPhase phase, float x, float y,
    fuchsia::ui::input::PointerEventType type = fuchsia::ui::input::PointerEventType::TOUCH,
    uint32_t buttons = 0);

bool CmpFloatingValues(float num1, float num2);

zx_koid_t ExtractKoid(const zx::object_base& object);

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref);

// Creates pointer event commands for one finger, where the pointer "device" is
// tied to one compositor. Helps remove boilerplate clutter.
//
// NOTE: It's easy to create an event stream with inconsistent state, e.g.,
// sending ADD ADD.  Client is responsible for ensuring desired usage.
class PointerCommandGenerator {
 public:
  PointerCommandGenerator(uint32_t compositor_id, uint32_t device_id, uint32_t pointer_id,
                          fuchsia::ui::input::PointerEventType type, uint32_t buttons = 0);

  fuchsia::ui::input::Command Add(float x, float y);
  fuchsia::ui::input::Command Down(float x, float y);
  fuchsia::ui::input::Command Move(float x, float y);
  fuchsia::ui::input::Command Up(float x, float y);
  fuchsia::ui::input::Command Remove(float x, float y);

 private:
  fuchsia::ui::input::Command MakeInputCommand(fuchsia::ui::input::PointerEvent event);

  uint32_t compositor_id_;
  fuchsia::ui::input::PointerEvent blank_;
};

}  // namespace integration_tests

#endif  // SRC_UI_SCENIC_INTEGRATION_TESTS_UTILS_H_
