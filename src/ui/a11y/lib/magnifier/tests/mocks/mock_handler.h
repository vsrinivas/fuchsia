// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_HANDLER_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_HANDLER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/time.h>

#include "src/lib/callback/scoped_task_runner.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/clip_space_transform.h"

namespace accessibility_test {

constexpr zx::duration kFramePeriod = zx::sec(1) / 60;

class MockHandler : public fuchsia::accessibility::testing::MagnificationHandler_TestBase {
 public:
  MockHandler();

  fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> NewBinding();

  const ClipSpaceTransform& transform() const { return transform_; }
  uint32_t update_count() const { return update_count_; }

 private:
  // |fuchsia::accessibility::testing::MagnificationHandler_TestBase|
  void NotImplemented_(const std::string& name) override;

  // |fuchsia::accessibility::MagnificationHandler|
  // Since this is called via FIDL channel, the test loop needs to be advanced in order for
  // transform updates to be surfaced.
  void SetClipSpaceTransform(float x, float y, float scale,
                             SetClipSpaceTransformCallback callback) override;

  fidl::Binding<fuchsia::accessibility::MagnificationHandler> binding_;
  ClipSpaceTransform transform_;
  uint32_t update_count_ = 0;
  callback::ScopedTaskRunner callback_runner_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_MOCK_HANDLER_H_
