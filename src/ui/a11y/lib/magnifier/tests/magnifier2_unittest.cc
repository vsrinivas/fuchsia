// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/zx/time.h>

#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"
#include "src/ui/a11y/lib/testing/formatting.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/epsilon.hpp>

namespace accessibility_test {
namespace {

class Magnifier2Test : public gtest::TestLoopFixture {
 public:
  Magnifier2Test() = default;
  ~Magnifier2Test() override = default;

  a11y::Magnifier2* magnifier() { return &magnifier_; }

 private:
  a11y::Magnifier2 magnifier_;
};

TEST_F(Magnifier2Test, RegisterHandler) {
  MockMagnificationHandler handler;
  magnifier()->RegisterHandler(handler.NewBinding());
  EXPECT_EQ(handler.transform(), ClipSpaceTransform::identity());
}

}  // namespace
}  // namespace accessibility_test
