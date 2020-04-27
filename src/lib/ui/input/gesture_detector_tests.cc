// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/input/gesture_detector.h"

namespace {

using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

enum class InteractionType { kUnknown, kPreTap, kTap, kDrag };

struct InteractionRecord {
  bool active = false;
  InteractionType interaction_type = InteractionType::kUnknown;
  input::GestureDetector::TapType tap_type;
  glm::vec2 coordinate;
  input::Gesture::Delta delta;
};

class TestInteraction : public input::GestureDetector::Interaction {
 public:
  TestInteraction(InteractionRecord* record) : record_(record) { *record_ = {.active = true}; }

  ~TestInteraction() override { record_->active = false; }

 private:
  void OnTapBegin(const glm::vec2& coordinate, input::GestureDetector::TapType tap_type) override {
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

class FakeDelegate : public input::GestureDetector::Delegate {
 public:
  void SetNextInteraction(std::unique_ptr<input::GestureDetector::Interaction> next_interaction) {
    next_interaction_ = std::move(next_interaction);
  }

 private:
  // |input::GestureDetector::Delegate|
  std::unique_ptr<input::GestureDetector::Interaction> BeginInteraction(
      const input::Gesture* gesture) override {
    EXPECT_TRUE(next_interaction_) << "Unexpected BeginInteraction";
    return std::move(next_interaction_);
  }

  std::unique_ptr<input::GestureDetector::Interaction> next_interaction_;
};

class GestureDetectorTest : public testing::Test {
 public:
  GestureDetectorTest() : gesture_detector_(&delegate_) {}

 protected:
  // Sets up a |TestInteraction| to be the next |Interaction| returned by
  // |BeginInteraction|.
  void RecordInteraction(InteractionRecord* record) {
    delegate_.SetNextInteraction(std::make_unique<TestInteraction>(record));
  }

  input::GestureDetector* gesture_detector() { return &gesture_detector_; }

 private:
  FakeDelegate delegate_;
  input::GestureDetector gesture_detector_;
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

  gesture_detector()->OnPointerEvent(mouse);

  // Move the pointer around past the drag threshold.
  mouse.x += 2 * input::GestureDetector::kDefaultDragThreshold;
  gesture_detector()->OnPointerEvent(mouse);
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
  gesture_detector()->OnPointerEvent(ptr);
  EXPECT_TRUE(ixn.active);

  ptr.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr);
  EXPECT_FALSE(ixn.active);
  // The interaction ends when the last pointer comes up.

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 1);
  EXPECT_EQ(ixn.coordinate, glm::vec2(0, 0));
}

TEST_F(GestureDetectorTest, TwoFingerTap) {
  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 1;
  ptr1.y = 0;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr1);

  ptr1.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr1);

  EXPECT_TRUE(ixn.active);
  ptr0.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr0);
  EXPECT_FALSE(ixn.active);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 2);
  EXPECT_EQ(ixn.coordinate, glm::vec2(0, 0));
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
  gesture_detector()->OnPointerEvent(ptr0);

  ptr0.x++;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 10;
  ptr1.y = 0;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr1);

  ptr1.x++;
  ptr1.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr1);

  ptr0.x--;
  gesture_detector()->OnPointerEvent(ptr0);

  // The tap should end after a pointer comes up...
  ptr0.x++;
  ptr0.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr0);
  EXPECT_TRUE(ixn.active);

  // ...after which further movement within the drag threshold should not
  // trigger a drag or a new interaction.
  ptr1.x--;
  gesture_detector()->OnPointerEvent(ptr1);

  EXPECT_TRUE(ixn.active);
  ptr1.x++;
  ptr1.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr1);
  EXPECT_FALSE(ixn.active);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);
  EXPECT_EQ(ixn.tap_type, 2);
  EXPECT_EQ(ixn.coordinate, glm::vec2(0, 0));
}

TEST_F(GestureDetectorTest, Drag) {
  InteractionRecord ixn;
  RecordInteraction(&ixn);

  auto ptr0 = kDefaultTouchEvent;
  ptr0.pointer_id = 0;
  ptr0.x = 0;
  ptr0.y = 0;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr0);

  ptr0.x += input::GestureDetector::kDefaultDragThreshold;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr0);

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
  gesture_detector()->OnPointerEvent(ptr1);

  EXPECT_EQ(ixn.tap_type, 2);

  ptr1.x++;
  ptr1.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr1);

  EXPECT_GT(ixn.delta.translation.x, input::GestureDetector::kDefaultDragThreshold);

  ptr0.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr0);

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
  gesture_detector()->OnPointerEvent(ptr0);

  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.x = 0;
  ptr1.y = 10;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr1);

  ptr1.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr1);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kTap);

  ptr0.x += dx;
  ptr0.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr0);

  EXPECT_EQ(ixn.interaction_type, InteractionType::kDrag);
  EXPECT_EQ(ixn.tap_type, 1);
  EXPECT_EQ(ixn.delta, input::Gesture::Delta({.translation = {dx, 0}, .rotation = 0, .scale = 1}));
}

// An |input::GestureDetector::Interaction| that owns and destroys its gesture detector on command,
// for testing lifecycle robustness. It's fragile to assume that an interaction will never destroy
// or |Reset()| its gesture detector on a callback.
class PoisonInteraction : public input::GestureDetector::Interaction {
 public:
  PoisonInteraction(std::unique_ptr<input::GestureDetector> gesture_detector)
      : gesture_detector_(std::move(gesture_detector)) {}

  // Sets the gesture detector to self destruct on the next event.
  void Poison() { poisoned_ = true; }

 private:
  void OnTapBegin(const glm::vec2&, input::GestureDetector::TapType) override { CheckPoison(); }

  void OnTapUpdate(input::GestureDetector::TapType) override { CheckPoison(); }

  void OnTapCommit() override { CheckPoison(); }

  void OnMultidrag(input::GestureDetector::TapType, const input::Gesture::Delta&) override {
    CheckPoison();
  }

  void CheckPoison() {
    if (poisoned_) {
      gesture_detector_ = nullptr;
    }
  }

  bool poisoned_ = false;
  std::unique_ptr<input::GestureDetector> gesture_detector_;
};

// These tests won't necessarily fail if the code is incorrect as referencing released memory is
// undefined behavior, but the allocator does catch some cases.
class PoisonInteractionTest : public testing::Test {
 public:
  PoisonInteractionTest() {
    auto gesture_detector = std::make_unique<input::GestureDetector>(&delegate);
    gesture_detector_ = gesture_detector.get();
    auto interaction = std::make_unique<PoisonInteraction>(std::move(gesture_detector));
    interaction_ = interaction.get();
    delegate.SetNextInteraction(std::move(interaction));
  }

  input::GestureDetector* gesture_detector() { return gesture_detector_; }
  PoisonInteraction* interaction() { return interaction_; }

 private:
  FakeDelegate delegate;
  input::GestureDetector* gesture_detector_;
  PoisonInteraction* interaction_;
};

TEST_F(PoisonInteractionTest, PoisonTapBegin) {
  interaction()->Poison();

  auto ptr = kDefaultTouchEvent;
  ptr.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr);
}

TEST_F(PoisonInteractionTest, PoisonTapUpdate) {
  auto ptr0 = kDefaultTouchEvent;
  ptr0.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr0);

  interaction()->Poison();
  auto ptr1 = kDefaultTouchEvent;
  ptr1.pointer_id = 1;
  ptr1.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr1);
}

TEST_F(PoisonInteractionTest, PoisonTapCommit) {
  auto ptr = kDefaultTouchEvent;
  ptr.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr);

  interaction()->Poison();
  ptr.phase = PointerEventPhase::UP;
  gesture_detector()->OnPointerEvent(ptr);
}

TEST_F(PoisonInteractionTest, PoisonMultidrag) {
  auto ptr = kDefaultTouchEvent;
  ptr.x = 0;
  ptr.y = 0;
  ptr.phase = PointerEventPhase::DOWN;
  gesture_detector()->OnPointerEvent(ptr);

  interaction()->Poison();
  ptr.x += input::GestureDetector::kDefaultDragThreshold;
  ptr.phase = PointerEventPhase::MOVE;
  gesture_detector()->OnPointerEvent(ptr);
}

class PoisonDelegate : public input::GestureDetector::Delegate {
 public:
  void SetGestureDetector(std::unique_ptr<input::GestureDetector> gesture_detector) {
    gesture_detector_ = std::move(gesture_detector);
  }

  input::GestureDetector* gesture_detector() { return gesture_detector_.get(); }

 private:
  // |input::GestureDetector::Delegate|
  std::unique_ptr<input::GestureDetector::Interaction> BeginInteraction(
      const input::Gesture* gesture) override {
    gesture_detector_ = nullptr;
    return nullptr;
  }

  std::unique_ptr<input::GestureDetector> gesture_detector_;
};

TEST(PoisonDelegateTest, PoisonBeginInteraction) {
  PoisonDelegate delegate;
  delegate.SetGestureDetector(std::make_unique<input::GestureDetector>(&delegate));

  auto ptr = kDefaultTouchEvent;
  ptr.phase = PointerEventPhase::DOWN;
  delegate.gesture_detector()->OnPointerEvent(ptr);
}

}  // namespace
