// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace mozart {
namespace scene {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  // TODO(MZ-124): We should derive an appropriate value from the rendering
  // targets, in particular giving priority to couple to the display refresh
  // (vsync).
  static constexpr uint64_t kHardcodedPresentationIntervalNanos = 16'666'667;

  static constexpr uint32_t kHardcodedDisplayWidth = 2160;
  static constexpr uint32_t kHardcodedDisplayHeight = 1440;
  static constexpr float kHardcodedDevicePixelRatio = 2.f;

  Display();

  // Obtain the time of the last Vsync, in nanoseconds.
  uint64_t GetLastVsyncTime() const;

  // Obtain the interval between Vsyncs.
  uint64_t GetVsyncInterval() const;

 private:
  uint64_t first_vsync_;
};

}  // namespace scene
}  // namespace mozart
