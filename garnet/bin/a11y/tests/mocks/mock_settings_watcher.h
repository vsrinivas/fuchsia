// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SETTINGS_WATCHER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SETTINGS_WATCHER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

namespace accessibility_test {

// This is a mock of Settings Watcher that gets notifications whenever
// Accessibility Settings are changed. This is used for testing Accessibility
// Settings.
class MockSettingsWatcher : public fuchsia::accessibility::SettingsWatcher {
 public:
  explicit MockSettingsWatcher(sys::testing::ComponentContextProvider* context);
  ~MockSettingsWatcher() = default;

  // |fuchsia::accessibility::SettingsWatcher|
  void OnSettingsChange(fuchsia::accessibility::Settings new_settings) override;

  // Compares given setting with the private member of this class.
  bool IsSame(fuchsia::accessibility::SettingsPtr provided_settings);

 private:
  bool CompareFloatArray(std::array<float, 9> first_array,
                         std::array<float, 9> second_array) const;
  void SaveSettings(fuchsia::accessibility::Settings provided_settings);

  fidl::BindingSet<fuchsia::accessibility::SettingsWatcher> bindings_;
  sys::testing::ComponentContextProvider* context_provider_;
  fuchsia::accessibility::SettingsManagerPtr manager_;
  fuchsia::accessibility::Settings settings_;
};

}  // namespace accessibility_test

#endif  //  GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SETTINGS_WATCHER_H_
