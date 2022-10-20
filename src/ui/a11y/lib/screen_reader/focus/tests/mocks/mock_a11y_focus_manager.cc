// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"

#include <zircon/types.h>

#include <climits>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "third_party/quickjs/cutils.h"

namespace accessibility_test {

std::optional<a11y::A11yFocusManager::A11yFocusInfo> MockA11yFocusManager::GetA11yFocus() {
  get_a11y_focus_called_ = true;
  if (!should_get_a11y_focus_fail_) {
    return a11y_focus_info_;
  }

  return std::nullopt;
}
void MockA11yFocusManager::SetA11yFocus(zx_koid_t koid, uint32_t node_id,
                                        a11y::A11yFocusManager::SetA11yFocusCallback callback) {
  set_a11y_focus_called_ = true;
  if (should_set_a11y_focus_fail_) {
    callback(false);
    return;
  }
  UpdateA11yFocus(koid, node_id);
  if (on_a11y_focus_updated_callback_) {
    on_a11y_focus_updated_callback_(GetA11yFocus());
  }
  callback(true);
}
void MockA11yFocusManager::ClearA11yFocus() {
  clear_a11y_focus_called_ = true;
  a11y_focus_info_.reset();
}

void MockA11yFocusManager::RestoreA11yFocusToInputFocus() {
  restore_a11y_focus_to_input_focus_called_ = true;
  if (restore_a11y_focus_to_input_focus_value_) {
    a11y_focus_info_ = restore_a11y_focus_to_input_focus_value_;
  }
}

void MockA11yFocusManager::RedrawHighlights() { redraw_highlights_called_ = true; }

bool MockA11yFocusManager::IsGetA11yFocusCalled() const { return get_a11y_focus_called_; }

bool MockA11yFocusManager::IsSetA11yFocusCalled() const { return set_a11y_focus_called_; }

bool MockA11yFocusManager::IsClearA11yFocusCalled() const { return clear_a11y_focus_called_; }

bool MockA11yFocusManager::IsRestoreA11yFocusToInputFocusCalled() const {
  return restore_a11y_focus_to_input_focus_called_;
}

bool MockA11yFocusManager::IsRedrawHighlightsCalled() const { return redraw_highlights_called_; }

void MockA11yFocusManager::set_should_get_a11y_focus_fail(bool value) {
  should_get_a11y_focus_fail_ = value;
}

void MockA11yFocusManager::set_should_set_a11y_focus_fail(bool value) {
  should_set_a11y_focus_fail_ = value;
}

void MockA11yFocusManager::set_restore_a11y_focus_to_input_focus_value(zx_koid_t koid,
                                                                       uint32_t node_id) {
  restore_a11y_focus_to_input_focus_value_ =
      A11yFocusInfo{.view_ref_koid = koid, .node_id = node_id};
}

void MockA11yFocusManager::UpdateA11yFocus(zx_koid_t koid, uint32_t node_id) {
  a11y_focus_info_.emplace(A11yFocusInfo{.view_ref_koid = koid, .node_id = node_id});
}

void MockA11yFocusManager::ResetExpectations() {
  get_a11y_focus_called_ = false;
  set_a11y_focus_called_ = false;
  restore_a11y_focus_to_input_focus_called_ = false;
  redraw_highlights_called_ = false;
  should_get_a11y_focus_fail_ = false;
  should_set_a11y_focus_fail_ = false;
}

}  // namespace accessibility_test
