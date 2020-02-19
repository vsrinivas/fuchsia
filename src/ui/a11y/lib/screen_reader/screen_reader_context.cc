// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include "src/lib/fxl/logging.h"

namespace a11y {

ScreenReaderContext::ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager)
    : a11y_focus_manager_(std::move(a11y_focus_manager)) {}

A11yFocusManager* ScreenReaderContext::GetA11yFocusManager() {
  FXL_DCHECK(a11y_focus_manager_.get());
  return a11y_focus_manager_.get();
}

}  // namespace a11y
