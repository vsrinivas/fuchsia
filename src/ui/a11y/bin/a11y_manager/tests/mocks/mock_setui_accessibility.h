// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SETUI_ACCESSIBILITY_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SETUI_ACCESSIBILITY_H_

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/fxl/macros.h"

namespace accessibility_test {

class MockSetUIAccessibility : public fuchsia::settings::Accessibility {
 public:
  explicit MockSetUIAccessibility(sys::testing::ComponentContextProvider* context);
  ~MockSetUIAccessibility() override;

  // |fuchsia.settings.Accessibility|
  void Watch(WatchCallback callback) override;

  // |fuchsia.settings.Accessibility|
  void Set(fuchsia::settings::AccessibilitySettings settings, SetCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::settings::Accessibility> bindings_;

  WatchCallback watchCallback_;

  bool first_watch_;
  fuchsia::settings::AccessibilitySettings settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSetUIAccessibility);
};
}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_SETUI_ACCESSIBILITY_H_
