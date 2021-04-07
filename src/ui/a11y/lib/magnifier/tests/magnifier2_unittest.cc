// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/zx/time.h>

#include <limits>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_handler.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"
#include "src/ui/a11y/lib/testing/formatting.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/epsilon.hpp>

namespace accessibility_test {
namespace {

using GestureType = a11y::GestureHandler::GestureType;
using testing::ElementsAre;

class Magnifier2Test : public gtest::RealLoopFixture {
 public:
  Magnifier2Test() = default;
  ~Magnifier2Test() override = default;

  a11y::Magnifier2* magnifier() { return magnifier_.get(); }

  MockMagnificationHandler* mock_magnification_handler() {
    return mock_magnification_handler_.get();
  }
  MockGestureHandler* mock_gesture_handler() { return mock_gesture_handler_.get(); }

  void SetUp() override {
    mock_gesture_handler_ = std::make_unique<MockGestureHandler>();
    mock_magnification_handler_ = std::make_unique<MockMagnificationHandler>();
    magnifier_ = std::make_unique<a11y::Magnifier2>();
    magnifier_->BindGestures(mock_gesture_handler_.get());
    magnifier_->RegisterHandler(mock_magnification_handler_->NewBinding());
  }

  void RunLoopUntilTransformIs(float x, float y, float scale) {
    // Run loop until each element of the transform is equal to the specified
    // values, accounting for potential float rounding error.
    RunLoopUntil([this, x, y, scale]() {
      const auto& transform = mock_magnification_handler()->transform();
      return std::abs(transform.x - x) < std::numeric_limits<float>::epsilon() &&
             std::abs(transform.y - y) < std::numeric_limits<float>::epsilon() &&
             std::abs(transform.scale - scale) < std::numeric_limits<float>::epsilon();
    });
  }

 private:
  std::unique_ptr<MockGestureHandler> mock_gesture_handler_;
  std::unique_ptr<MockMagnificationHandler> mock_magnification_handler_;
  std::unique_ptr<a11y::Magnifier2> magnifier_;
};

TEST_F(Magnifier2Test, RegisterHandler) {
  EXPECT_EQ(mock_magnification_handler()->transform(), ClipSpaceTransform::identity());
}

TEST_F(Magnifier2Test, GestureHandlersAreRegisteredIntheRightOrder) {
  // The order in which magnifier gestures are registered is relevant.
  EXPECT_THAT(mock_gesture_handler()->bound_gestures(),
              ElementsAre(GestureType::kOneFingerTripleTap, GestureType::kThreeFingerDoubleTap,
                          GestureType::kOneFingerTripleTapDrag,
                          GestureType::kThreeFingerDoubleTapDrag, GestureType::kTwoFingerDrag));
}

TEST_F(Magnifier2Test, OneFingerTripleTapTogglesMagnification) {
  a11y::GestureContext gesture_context;
  gesture_context.current_pointer_locations[1].ndc_point.x = 0.4f;
  gesture_context.current_pointer_locations[1].ndc_point.y = 0.5f;
  mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);
  RunLoopUntilTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);

  mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap);
  RunLoopUntilTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

TEST_F(Magnifier2Test, ThreeFingerDoubleTapTogglesMagnification) {
  a11y::GestureContext gesture_context;
  gesture_context.current_pointer_locations[1].ndc_point.x = 0.3f;
  gesture_context.current_pointer_locations[1].ndc_point.y = 0.4f;
  gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
  gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
  gesture_context.current_pointer_locations[3].ndc_point.x = 0.5f;
  gesture_context.current_pointer_locations[3].ndc_point.y = 0.6f;
  mock_gesture_handler()->TriggerGesture(GestureType::kThreeFingerDoubleTap, gesture_context);
  RunLoopUntilTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);

  mock_gesture_handler()->TriggerGesture(GestureType::kThreeFingerDoubleTap);
  RunLoopUntilTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

}  // namespace
}  // namespace accessibility_test
