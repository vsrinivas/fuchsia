// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_FOCUSER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_FOCUSER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

namespace accessibility_test {
// Mocks fuchsia::ui::views::focuser which is used for requesting focus change to a specific
// view using ViewRef.
class MockFocuser : public fuchsia::ui::views::Focuser {
 public:
  MockFocuser() = default;

  ~MockFocuser() override = default;

  // Helper function for changing focus_request_received_.
  void SetFocusRequestReceived(bool focus_received);

  // Helper function for getting focus_request_received_.
  bool GetFocusRequestReceived() const;

  // Returns the koid of view_ref on which RequestFocus() was called.
  zx_koid_t GetViewRefKoid() const;

  // Function for setting throw_error_ flag.
  void SetThrowError(bool throw_error);

 private:
  // |fuchsia::ui::views::Focuser|
  void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) override;

  // Flag used for knowing if RequestFocus() is called.
  bool focus_request_received_ = false;

  // ViewRef on which focus is requested.
  fuchsia::ui::views::ViewRef view_ref_;

  // Flag to know if RequestFocus() should throw error.
  bool throw_error_ = false;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_TESTS_MOCKS_MOCK_FOCUSER_H_
