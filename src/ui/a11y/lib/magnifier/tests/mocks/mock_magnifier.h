// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_MAGNIFIER_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_MAGNIFIER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/cpp/fidl_test_base.h>

namespace accessibility_test {

class MockMagnifier : public fuchsia::accessibility::testing::Magnifier_TestBase {
 public:
  fuchsia::accessibility::MagnificationHandlerPtr& handler() { return handler_; }

 private:
  // |fuchsia::accessibility::testing::Magnifier_TestBase|
  void NotImplemented_(const std::string& name) override;

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override;

  fuchsia::accessibility::MagnificationHandlerPtr handler_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_MAGNIFIER_H_
