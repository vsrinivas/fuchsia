// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/syslog/cpp/logger.h>

#include "src/lib/ui/input/gesture_detector.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

// gtest blocks top-level namespace lookup of overloaded ==
namespace fuchsia {
namespace ui {
namespace gfx {

bool operator==(const vec2& a, const vec2& b) { return fidl::Equals(a, b); }

}  // namespace gfx
}  // namespace ui
}  // namespace fuchsia

namespace {

using fuchsia::ui::gfx::vec2;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

enum class InteractionType { kUnknown, kPreTap, kTap, kDrag };

struct InteractionRecord {
  bool active = false;
  InteractionType interaction_type = InteractionType::kUnknown;
  input::GestureDetector::TapType tap_type;
  vec2 coordinate;
  input::Gesture::Delta delta;
};

class TestInteraction : public input::GestureDetector::Interaction {
 public:
  TestInteraction(InteractionRecord* record) : record_(record) { *record_ = {.active = true}; }

  ~TestInteraction() override { record_->active = false; }

 private:
  void OnTapBegin(const vec2& coordinate, input::GestureDetector::TapType tap_type) override {
    EXPECT_EQ(record_->interaction_type, InteractionType::kUnknown);
    record_->interaction_type = InteractionType::kPreTap;
    record_->coordinate = coordinate;
    record_->tap_type = tap_type;
  }

  void OnTapUpdate(input::GestureDetector::TapType tap_type) override {
    EXPECT_EQ(record_->interaction_type, InteractionType::kPreTap);
    record_->tap_type = tap_type;
  }

  void OnTapCommit() override {
    EXPECT_EQ(record_->interaction_type, InteractionType::kPreTap);
    record_->interaction_type = InteractionType::kTap;
  }

  void OnMultidrag(input::GestureDetector::TapType tap_type,
                   const input::Gesture::Delta& delta) override {
    record_->interaction_type = InteractionType::kDrag;
    record_->tap_type = tap_type;
    record_->delta += delta;
  }

  InteractionRecord* record_;
};

class GestureDetectorTest : public testing::Test, input::GestureDetector::Delegate {
 public:
  GestureDetectorTest() : gesture_detector_(this) {}

 protected:
  // Sets up a |TestInteraction| to be the next |Interaction| returned by
  // |BeginInteraction|.
  void RecordInteraction(InteractionRecord* record) {
    next_interaction_ = std::make_unique<TestInteraction>(record);
  }

  input::GestureDetector gesture_detector_;

 private:
  // |input::GestureDetector::Delegate|
  std::unique_ptr<input::GestureDetector::Interaction> BeginInteraction(
      const input::Gesture* gesture) override {
    EXPECT_TRUE(next_interaction_) << "Unexpected BeginInteraction";
    return std::move(next_interaction_);
  }

  std::unique_ptr<input::GestureDetector::Interaction> next_interaction_;
};

TEST_F(GestureDetectorTest, IgnoreMouseHover) {
  PointerEvent mouse = {.device_id = 0,
                        .pointer_id = 0,
                        .type = PointerEventType::MOUSE,
                        .phase = PointerEventPhase::MOVE,
                        .x = 0,
                        .y = 0};

  // If we erroneously create a new interaction at any point, this will violate
  // the expectation in |GestureDetectorTest::BeginInteraction|.

  gesture_detector_.OnPointerEvent(mouse);

  // Move the pointer around past the drag threshold.
  mouse.x += 2 * input::GestureDetector::kDefaultDragThreshold;
  gesture_detector_.OnPointerEvent(mouse);
}

// A default starting point for touch events. Usages should explicitly set any
// members (other than |.type|) that they care about.
constexpr PointerEvent kDefaultTouchEvent = {
    .device_id = 0, .pointer_id = 0, .type = PointerEventType::TOUCH};

TEST_F(GestureDetectorTest, Tap) {
  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr = kDefaultTouchEvent;
  ptr.x = 0;
  ptr.y = 0;
  ptr.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr);
  EXPECT_TRUE(ixn.active);

  ptr.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr);
  EXPECT_FALSE(ixn.active);
  // The interaction ends when the last pointer comes up.

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 1);
  EXPECT_EQ(ixn.coordinate, vec2({0, 0}));
}

TEST_F(GestureDetectorTest, TwoFingerTap) {
  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 1;
  ptr1.y = 0;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr1);

  ptr1.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr1);

  EXPECT_TRUE(ixn.active);
  ptr0.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr0);
  EXPECT_FALSE(ixn.active);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 2);
  EXPECT_EQ(ixn.coordinate, vec2({0, 0}));
}

TEST_F(GestureDetectorTest, TwoFingerTapWithDrift) {
  FX_CHECK(1 < input::GestureDetector::kDefaultDragThreshold)
      << "kDefaultDragThreshold is too low; rewrite this test to override.";

  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr0);

  ptr0.x++;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector_.OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 10;
  ptr1.y = 0;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr1);

  ptr1.x++;
  ptr1.phase = PointerEventPhase::MOVE;
  gesture_detector_.OnPointerEvent(ptr1);

  ptr0.x--;
  gesture_detector_.OnPointerEvent(ptr0);

  // The tap should end after a pointer comes up...
  ptr0.x++;
  ptr0.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr0);
  EXPECT_TRUE(ixn.active);

  // ...after which further movement within the drag threshold should not
  // trigger a drag or a new interaction.
  ptr1.x--;
  gesture_detector_.OnPointerEvent(ptr1);

  EXPECT_TRUE(ixn.active);
  ptr1.x++;
  ptr1.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr1);
  EXPECT_FALSE(ixn.active);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 2);
  EXPECT_EQ(ixn.coordinate, vec2({0, 0}));
}

TEST_F(GestureDetectorTest, Drag) {
  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr0);

  ptr0.x += input::GestureDetector::kDefaultDragThreshold;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector_.OnPointerEvent(ptr0);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kDrag);
  EXPECT_EQ(ixn.tap_type, 1);
  EXPECT_EQ(ixn.delta, input::Gesture::Delta(
                           {.translation = {input::GestureDetector::kDefaultDragThreshold, 0},
                            .rotation = 0,
                            .scale = 1}));

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 0;
  ptr1.y = 10;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr1);

  EXPECT_EQ(ixn.tap_type, 2);

  ptr1.x++;
  ptr1.phase = PointerEventPhase::MOVE;
  gesture_detector_.OnPointerEvent(ptr1);

  EXPECT_GT(ixn.delta.translation.x, input::GestureDetector::kDefaultDragThreshold);

  ptr0.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr0);

  EXPECT_EQ(ixn.tap_type, 1);
}

// This covers the case where a tap has been committed due to a pointer release
// but the remaining pointer is dragged past the threshold.
TEST_F(GestureDetectorTest, TapIntoDrag) {
  const float dx = 2 * input::GestureDetector::kDefaultDragThreshold;

  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 0;
  ptr1.y = 10;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector_.OnPointerEvent(ptr1);

  ptr1.phase = PointerEventPhase::UP;
  gesture_detector_.OnPointerEvent(ptr1);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);

  ptr0.x += dx;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector_.OnPointerEvent(ptr0);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kDrag);
  EXPECT_EQ(ixn.tap_type, 1);
  EXPECT_EQ(ixn.delta, input::Gesture::Delta({.translation = {dx, 0}, .rotation = 0, .scale = 1}));
}

}  // namespace
