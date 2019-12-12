// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"

#include <gtest/gtest.h>

namespace accessibility_test {

MockMagnificationHandler::MockMagnificationHandler() : binding_(this) {}

fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler>
MockMagnificationHandler::NewBinding() {
  return binding_.NewBinding();
}

void MockMagnificationHandler::NotImplemented_(const std::string& name) {
  FAIL() << name << " is not implemented";
}

void MockMagnificationHandler::SetClipSpaceTransform(float x, float y, float scale,
                                                     SetClipSpaceTransformCallback callback) {
  transform_ = {x, y, scale};
  ++update_count_;

  // Simulate presentation at 60 FPS to test our animation timings. In our test fixtures, the
  // default dispatcher will be the test dispatcher.
  callback_runner_.PostDelayedTask(std::move(callback), kFramePeriod);
}

}  // namespace accessibility_test
