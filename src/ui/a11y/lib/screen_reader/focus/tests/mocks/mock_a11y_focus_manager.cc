// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"

#include <zircon/types.h>

#include <climits>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "third_party/quickjs/cutils.h"

namespace accessibility_test {
MockA11yFocusManager::MockA11yFocusManager() {
  a11y_focus_info_.view_ref_koid = ZX_KOID_INVALID;
  a11y_focus_info_.node_id = UINT_MAX;
}

std::optional<a11y::A11yFocusManager::A11yFocusInfo> MockA11yFocusManager::GetA11yFocus() {
  get_a11y_focus_called_ = true;
  return a11y_focus_info_;
}

void MockA11yFocusManager::SetA11yFocus(zx_koid_t koid, uint32_t node_id,
                                        a11y::A11yFocusManager::SetA11yFocusCallback callback) {
  set_a11y_focus_called_ = true;
  a11y_focus_info_.view_ref_koid = koid;
  a11y_focus_info_.node_id = node_id;
  callback(true);
}

bool MockA11yFocusManager::IsGetA11yFocusCalled() const { return get_a11y_focus_called_; }

bool MockA11yFocusManager::IsSetA11yFocusCalled() const { return set_a11y_focus_called_; }

}  // namespace accessibility_test
