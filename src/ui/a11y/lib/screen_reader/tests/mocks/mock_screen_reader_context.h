// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_

#include <memory>

#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace accessibility_test {

class MockScreenReaderContext : public a11y::ScreenReaderContext {
 public:
  MockScreenReaderContext();
  ~MockScreenReaderContext() override = default;

  // Pointer to the mock, so expectations can be configured in tests.
  MockA11yFocusManager* mock_a11y_focus_manager_ptr() { return mock_a11y_focus_manager_ptr_; }

  // |ScreenReaderContext|
  a11y::A11yFocusManager* GetA11yFocusManager() override { return a11y_focus_manager_.get(); }

 private:
  std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager_;
  MockA11yFocusManager* mock_a11y_focus_manager_ptr_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_MOCKS_MOCK_SCREEN_READER_CONTEXT_H_
