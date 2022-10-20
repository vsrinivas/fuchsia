// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_A11Y_FOCUS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_A11Y_FOCUS_MANAGER_H_

#include <optional>

#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_listener.h"
#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"

namespace accessibility_test {
class MockA11yFocusManager : public a11y::A11yFocusManager {
 public:
  ~MockA11yFocusManager() override = default;

  // Returns the current a11y focus.
  std::optional<a11y::A11yFocusManager::A11yFocusInfo> GetA11yFocus() override;

  // |A11yFocusManager|
  void RestoreA11yFocusToInputFocus() override;

  // Function for setting a11y focus.
  void SetA11yFocus(zx_koid_t koid, uint32_t node_id,
                    a11y::A11yFocusManager::SetA11yFocusCallback callback) override;

  // |A11yFocusManager|
  void ClearA11yFocus() override;

  // |A11yFocusManager|
  void RedrawHighlights() override;

  // |A11yFocusManager|
  void set_on_a11y_focus_updated_callback(
      OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback) override {
    on_a11y_focus_updated_callback_ = std::move(on_a11y_focus_updated_callback);
  }

  // Returns true if GetA11yFocus was called.
  bool IsGetA11yFocusCalled() const;

  // Returns true if SetA11yFocus was called.
  bool IsSetA11yFocusCalled() const;

  // Returns true if ClearA11yFocus was called.
  bool IsClearA11yFocusCalled() const;

  // Returns true if RestoreA11yFocusToInputFocus was called.
  bool IsRestoreA11yFocusToInputFocusCalled() const;

  // Returns true if RedrawHighlights() was called.
  bool IsRedrawHighlightsCalled() const;

  // Resets the IsCalled* methods return values (useful for reading expectations on an object,
  // clearing the values, and running new expectations).
  void ResetExpectations();

  // Updates A11yFocusInfo with the given values.
  void UpdateA11yFocus(zx_koid_t koid, uint32_t node_id);

  void set_should_get_a11y_focus_fail(bool value);
  void set_should_set_a11y_focus_fail(bool value);

  // Set the value that the a11y focus should be changed to if
  // RestoreA11yFocusToInputFocus is called.
  void set_restore_a11y_focus_to_input_focus_value(zx_koid_t koid, uint32_t node_id);

 private:
  // Tracks if GetA11yFocus() is called.
  bool get_a11y_focus_called_ = false;

  // Tracks if SetA11yFocus() is called.
  bool set_a11y_focus_called_ = false;

  // Tracks if ClearA11yFocus() is called.
  bool clear_a11y_focus_called_ = false;

  // Tracks if RestoreA11yFocusToInputFocus() is called.
  bool restore_a11y_focus_to_input_focus_called_ = false;

  // Tracks if RedrawHighlights() is called.
  bool redraw_highlights_called_ = false;

  // Whether GetA11yFocus() call should fail.
  bool should_get_a11y_focus_fail_ = false;

  // Whether SetA11yFocus() call should fail.
  bool should_set_a11y_focus_fail_ = false;

  std::optional<a11y::A11yFocusManager::A11yFocusInfo> restore_a11y_focus_to_input_focus_value_;

  OnA11yFocusUpdatedCallback on_a11y_focus_updated_callback_;

  std::optional<a11y::A11yFocusManager::A11yFocusInfo> a11y_focus_info_;
};
}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_A11Y_FOCUS_MANAGER_H_
