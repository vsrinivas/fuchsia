// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"

#include <lib/syslog/cpp/macros.h>

namespace accessibility_test {

MockScreenReaderContext::MockScreenReaderContext() : ScreenReaderContext() {
  auto a11y_focus_manager = std::make_unique<MockA11yFocusManager>();
  mock_a11y_focus_manager_ptr_ = a11y_focus_manager.get();
  a11y_focus_manager_ = std::move(a11y_focus_manager);
}

}  // namespace accessibility_test
