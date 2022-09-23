// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace a11y {

// Talks to `fuchsia.ui.pointer.augment.TouchSourceWithLocalHit.Watch`
// to receive touch events and participate in gesture disambiguation.
//
// Starts calling `Watch` immediately upon construction.
//
// NOTE: this is just a skeleton, and not instantiated anywhere yet.
class GestureManagerV2 {
 public:
  explicit GestureManagerV2(fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source);

 private:
  // Call `TouchSourceWithLocalHit.Watch` repeatedly, responding to touch events.
  //
  // Currently, the implementation just "accepts" all events.
  void WatchForTouchEvents(std::vector<fuchsia::ui::pointer::TouchResponse> responses);

  fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source_;

  std::optional<int32_t> touch_device_id_;

  std::optional<fuchsia::ui::pointer::ViewParameters> view_parameters_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_MANAGER_V2_H_
