// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_handler.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"

namespace accessibility_test {
namespace {

using GestureType = a11y::GestureHandler::GestureType;
using testing::ElementsAre;

class MockMagnifierDelegate : public a11y::Magnifier2::Delegate {
 public:
  MockMagnifierDelegate() = default;
  ~MockMagnifierDelegate() override = default;

  void SetMagnificationTransform(float scale, float x, float y,
                                 SetMagnificationTransformCallback callback) override {
    scale_ = scale;
    x_ = x;
    y_ = y;
    callback();
  }

  float scale() { return scale_; }
  float x() { return x_; }
  float y() { return y_; }

 private:
  // Current state of the transform.
  float scale_ = 1.f;
  float x_ = 0.f;
  float y_ = 0.f;
};

class Magnifier2Test : public ::testing::Test {
 public:
  Magnifier2Test() = default;
  ~Magnifier2Test() override = default;

  a11y::Magnifier2* magnifier() { return magnifier_.get(); }

  MockMagnifierDelegate* mock_magnifier_delegate() { return mock_magnifier_delegate_; }

  MockGestureHandler* mock_gesture_handler() { return mock_gesture_handler_.get(); }

  void SetUp() override {
    mock_gesture_handler_ = std::make_unique<MockGestureHandler>();
    auto mock_magnifier_delegate = std::make_unique<MockMagnifierDelegate>();
    mock_magnifier_delegate_ = mock_magnifier_delegate.get();
    magnifier_ = std::make_unique<a11y::Magnifier2>(std::move(mock_magnifier_delegate));
    magnifier_->BindGestures(mock_gesture_handler_.get());
  }

  void ExpectThatTransformIs(float x, float y, float scale) {
    auto true_x = mock_magnifier_delegate_->x();
    auto true_y = mock_magnifier_delegate_->y();
    auto true_scale = mock_magnifier_delegate_->scale();
    FX_LOGS(INFO) << "true_x: " << true_x << " true_y: " << true_y << " true_scale: " << true_scale;
    FX_LOGS(INFO) << "expected_x: " << x << " expected_y: " << y << " expected_scale: " << scale;
    FX_LOGS(INFO) << "Difference: " << std::abs(true_x - x) << " " << std::abs(true_y - y) << " "
                  << std::abs(true_scale - scale) << " epsilon "
                  << scale * std::numeric_limits<float>::epsilon();
    EXPECT_FLOAT_EQ(mock_magnifier_delegate_->x(), x);
    EXPECT_FLOAT_EQ(mock_magnifier_delegate_->y(), y);
    EXPECT_FLOAT_EQ(mock_magnifier_delegate_->scale(), scale);
  }

 private:
  std::unique_ptr<MockGestureHandler> mock_gesture_handler_;
  std::unique_ptr<a11y::Magnifier2> magnifier_;

  // Owned by magnifier_.
  MockMagnifierDelegate* mock_magnifier_delegate_;
};

TEST_F(Magnifier2Test, RegisterHandler) {
  EXPECT_EQ(mock_magnifier_delegate()->x(), 0.f);
  EXPECT_EQ(mock_magnifier_delegate()->y(), 0.f);
  EXPECT_EQ(mock_magnifier_delegate()->scale(), 1.f);
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
  ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                        -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                        a11y::Magnifier2::kDefaultScale);

  mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap);
  ExpectThatTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
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
  ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                        -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                        a11y::Magnifier2::kDefaultScale);

  mock_gesture_handler()->TriggerGesture(GestureType::kThreeFingerDoubleTap);
  ExpectThatTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

TEST_F(Magnifier2Test, ThreeFingerDoubleTapDragTogglesTemporaryMagnification) {
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.3f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
    gesture_context.current_pointer_locations[3].ndc_point.x = 0.5f;
    gesture_context.current_pointer_locations[3].ndc_point.y = 0.6f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kThreeFingerDoubleTapDrag,
                                                    gesture_context);

    ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.1f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.2f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.2f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.3f;
    gesture_context.current_pointer_locations[3].ndc_point.x = 0.3f;
    gesture_context.current_pointer_locations[3].ndc_point.y = 0.4f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kThreeFingerDoubleTapDrag,
                                                 gesture_context);

    ExpectThatTransformIs(-.2f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.3f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  mock_gesture_handler()->TriggerGestureComplete(GestureType::kThreeFingerDoubleTapDrag);

  ExpectThatTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

TEST_F(Magnifier2Test, OneFingerTripleTapDragTogglesTemporaryMagnification) {
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.3f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.4f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kOneFingerTripleTapDrag,
                                                    gesture_context);

    ExpectThatTransformIs(-.3f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.4f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.1f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.2f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kOneFingerTripleTapDrag,
                                                 gesture_context);

    ExpectThatTransformIs(-.1f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.2f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  mock_gesture_handler()->TriggerGestureComplete(GestureType::kOneFingerTripleTapDrag);

  ExpectThatTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

TEST_F(Magnifier2Test, TwoFingerDrag) {
  // One-finger-triple-tap to enter persistent magnification mode.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);

    ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  // Begin two-finger drag at a point different from the current magnification
  // focus to ensure that the transform does not change.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.2f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.3f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kTwoFingerDrag, gesture_context);

    ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  // Scale and pan.
  {
    a11y::GestureContext gesture_context;
    // Double average distance between the fingers and the centroid, and
    // translate the centroid from (.3, .4) to (.2, .3).
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.0f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.1f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kTwoFingerDrag, gesture_context);

    // The average distance between the fingers and the centroid doubled, so the
    // scale should double.
    auto new_scale = a11y::Magnifier2::kDefaultScale * 2;
    // The new transform should ensure that the point under the centroid of the
    // user's fingers moves with the centroid of the two-finger drag. Since the
    // drag started with a centroid of (.3, .4) and now has a centroid of (.2,
    // 3.), applying the transform to the point in unscaled NDC space that
    // corresponds to (.3, .4) in the default zoom space should yield (.2, .3).
    // We can find the NDC point that corresponds to (.3, .4) by simply applying
    // the inverse of the transform for that space, which we verified previously
    // had a scale of kDefaultScale 4, and a translation of (-1.2, -1.5). So,
    // applying the inverse of this transform to (.3, .4) gives us (.375, .475)
    // in the NDC space. Since the new scale is kDefaultScale * 2 = 8, we can
    // solve for the new translation solving this equation for new_translation:
    // (.2, .3) = 8 * (.375, .475) + new_translation
    ExpectThatTransformIs(-2.8f /* x */, -3.5f /* y */, new_scale);
  }
}

TEST_F(Magnifier2Test, ZoomOutIfMagnified) {
  // Magnify to some non-trivial transform state.
  a11y::GestureContext gesture_context;
  gesture_context.current_pointer_locations[1].ndc_point.x = 0.4f;
  gesture_context.current_pointer_locations[1].ndc_point.y = 0.5f;
  mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);
  ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                        -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                        a11y::Magnifier2::kDefaultScale);

  // Call ZoomOutIfMagnified() to ensure that we return to "normal" zoom state.
  magnifier()->ZoomOutIfMagnified();
  ExpectThatTransformIs(0 /* x */, 0 /* y */, 1 /* scale */);
}

TEST_F(Magnifier2Test, ClampPan) {
  // One-finger-triple-tap to enter persistent magnification mode.
  // Focus on the top-right corner of the screen.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 1.f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 1.f;
    mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);

    ExpectThatTransformIs(-1.f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -1.f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  // Begin a two-finger drag.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 1.f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 1.f;
    gesture_context.current_pointer_locations[2].ndc_point.x = .9f;
    gesture_context.current_pointer_locations[2].ndc_point.y = .9f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kTwoFingerDrag, gesture_context);
  }

  // Drag down and to the left. Since the focus is already on the top right
  // corner, this gesture should have no effect on the transform.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.1f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.1f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.0f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.0f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kTwoFingerDrag, gesture_context);

    ExpectThatTransformIs(-1.f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -1.f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }
}

TEST_F(Magnifier2Test, ClampZoom) {
  // One-finger-triple-tap to enter persistent magnification mode.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0;
    mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);

    ExpectThatTransformIs(0 /* x */, 0 /* y */, a11y::Magnifier2::kDefaultScale);
  }

  // Begin a two-finger drag with fingers very close together.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = .01f;
    gesture_context.current_pointer_locations[1].ndc_point.y = .01f;
    gesture_context.current_pointer_locations[2].ndc_point.x = -.01f;
    gesture_context.current_pointer_locations[2].ndc_point.y = -.01f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kTwoFingerDrag, gesture_context);
  }

  // Spread fingers far apart. The scale should be capped at kMaxScale.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 1.f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 1.f;
    gesture_context.current_pointer_locations[2].ndc_point.x = -1.f;
    gesture_context.current_pointer_locations[2].ndc_point.y = -1.f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kTwoFingerDrag, gesture_context);

    ExpectThatTransformIs(0 /* x */, 0 /* x */, a11y::Magnifier2::kMaxScale);
  }
}

TEST_F(Magnifier2Test, TwoFingerDragOnlyWorksInPersistentMode) {
  // The magnifier should only respond to two-finger drags when in PERSISTENT
  // mode, so the magnification transform should not change during this test
  // case.
  //
  // Begin two-finger drag at a point different from the current magnification
  // focus to ensure that the transform does not change.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.2f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.3f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kTwoFingerDrag, gesture_context);

    ExpectThatTransformIs(0 /* x */, 0 /* x */, 1.f);
  }

  // Try to scale and pan, and again, verify that the transform does not change.
  {
    a11y::GestureContext gesture_context;
    // Double average distance between the fingers and the centroid, and
    // translate the centroid from (.3, .4) to (.2, .3).
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.0f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.1f;
    gesture_context.current_pointer_locations[2].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[2].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGestureUpdate(GestureType::kTwoFingerDrag, gesture_context);

    ExpectThatTransformIs(0 /* x */, 0 /* x */, 1.f);
  }
}

TEST_F(Magnifier2Test, TapDragOnlyWorksInUnmagnifiedMode) {
  // The magnifier should not respond to tap-drag gestures when in PERSISTENT
  // mode, so the magnification transform should not change during this test
  // case.
  //
  // Enter PERSISTENT mode with a one-finger-triple-tap.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.4f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.5f;
    mock_gesture_handler()->TriggerGesture(GestureType::kOneFingerTripleTap, gesture_context);
    ExpectThatTransformIs(-.4f * (a11y::Magnifier2::kDefaultScale - 1),  // x translation
                          -.5f * (a11y::Magnifier2::kDefaultScale - 1),  // y translation
                          a11y::Magnifier2::kDefaultScale);
  }

  // Attempt a one-finger-triple-tap-drag at a different location. The
  // magnifier should ignore the gesture, so the transform should not change.
  {
    a11y::GestureContext gesture_context;
    gesture_context.current_pointer_locations[1].ndc_point.x = 0.3f;
    gesture_context.current_pointer_locations[1].ndc_point.y = 0.4f;
    mock_gesture_handler()->TriggerGestureRecognize(GestureType::kOneFingerTripleTapDrag,
                                                    gesture_context);

    // Check that the translation has not changed. X and Y translations are
    // updated together, so checking one of them is sufficient.
    EXPECT_LT(std::abs(mock_magnifier_delegate()->x() - (-1.2f)),
              a11y::Magnifier2::kDefaultScale * std::numeric_limits<float>::epsilon());
  }
}

}  // namespace
}  // namespace accessibility_test
