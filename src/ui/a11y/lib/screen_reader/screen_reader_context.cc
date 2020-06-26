// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ScreenReaderContext::ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                                         std::string locale_id)
    : executor_(async_get_default_dispatcher()),
      a11y_focus_manager_(std::move(a11y_focus_manager)),
      locale_id_(std::move(locale_id)) {}

A11yFocusManager* ScreenReaderContext::GetA11yFocusManager() {
  FX_DCHECK(a11y_focus_manager_.get());
  return a11y_focus_manager_.get();
}

}  // namespace a11y
