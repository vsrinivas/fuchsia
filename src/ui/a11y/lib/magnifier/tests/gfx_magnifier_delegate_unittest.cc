// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/gfx_magnifier_delegate.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"

namespace accessibility_test {
namespace {

class GfxMagnifierDelegateTest : public gtest::RealLoopFixture {
 public:
  GfxMagnifierDelegateTest() = default;
  ~GfxMagnifierDelegateTest() override = default;

  void SetUp() override {
    mock_magnification_handler_ = std::make_unique<MockMagnificationHandler>();
    gfx_magnifier_delegate_ = std::make_unique<a11y::GfxMagnifierDelegate>();
    gfx_magnifier_delegate_->RegisterHandler(mock_magnification_handler_->NewBinding());
  }

  MockMagnificationHandler* mock_magnification_handler() {
    return mock_magnification_handler_.get();
  }

  a11y::GfxMagnifierDelegate* gfx_magnifier_delegate() { return gfx_magnifier_delegate_.get(); }

 private:
  std::unique_ptr<MockMagnificationHandler> mock_magnification_handler_;
  std::unique_ptr<a11y::GfxMagnifierDelegate> gfx_magnifier_delegate_;
};

TEST_F(GfxMagnifierDelegateTest, SetMagnificationTransform) {
  EXPECT_EQ(mock_magnification_handler()->transform(), ClipSpaceTransform::identity());

  bool transform_set = false;
  const float transform_x = 2.f;
  const float transform_y = 3.f;
  const float transform_scale = 4.f;
  gfx_magnifier_delegate()->SetMagnificationTransform(transform_scale, transform_x, transform_y,
                                                      [&transform_set] { transform_set = true; });

  RunLoopUntil([&transform_set] { return transform_set; });

  const auto& transform = mock_magnification_handler()->transform();
  EXPECT_FLOAT_EQ(transform.x, transform_x);
  EXPECT_FLOAT_EQ(transform.y, transform_y);
  EXPECT_FLOAT_EQ(transform.scale, transform_scale);
}

}  // namespace
}  // namespace accessibility_test
