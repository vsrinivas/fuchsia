// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_
#define SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_

#include <optional>

#include "src/ui/a11y/lib/view/a11y_view.h"

namespace accessibility_test {

class MockAccessibilityView : public a11y::AccessibilityViewInterface {
 public:
  MockAccessibilityView() = default;
  ~MockAccessibilityView() override = default;

  // |AccessibilityViewInterface |
  std::optional<fuchsia::ui::gfx::ViewProperties> get_a11y_view_properties() override {
    return a11y_view_properties_;
  }

  void set_a11y_view_properties(
      std::optional<fuchsia::ui::gfx::ViewProperties> a11y_view_properties) {
    a11y_view_properties_ = a11y_view_properties;
  }

  // |AccessibilityViewInterface |
  std::optional<fuchsia::ui::views::ViewRef> view_ref() override { return std::move(view_ref_); }

  void set_view_ref(std::optional<fuchsia::ui::views::ViewRef> view_ref) {
    view_ref_ = std::move(view_ref);
  }

  // |AccessibilityViewInterface |
  void add_view_properties_changed_callback(ViewPropertiesChangedCallback callback) override {
    callback_ = std::move(callback);
  }

  void add_scene_ready_callback(SceneReadyCallback callback) override {}

 private:
  std::optional<fuchsia::ui::gfx::ViewProperties> a11y_view_properties_;
  std::optional<fuchsia::ui::views::ViewRef> view_ref_;
  ViewPropertiesChangedCallback callback_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_ACCESSIBILITY_VIEW_H_
