// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_ARENA_MEMBER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_ARENA_MEMBER_H_

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
namespace accessibility_test {

class MockArenaMember : public a11y::ArenaMember {
 public:
  explicit MockArenaMember(a11y::GestureRecognizer* recognizer);

  // Recognizer will call this function to declare defeat.
  bool DeclareDefeat();

  bool IsDeclareDefeatCalled() { return declare_defeat_called_; }

  // Recognizer will call this function when gesture is executed.
  bool StopRoutingPointerEvents(fuchsia::ui::input::accessibility::EventHandling handled);

  bool IsStopRoutingPointerEventsCalled() { return stop_routing_pointer_events_called_; }

  // Helper function to call OnWin() on recognizer.
  bool CallOnWin();

  // Returns if OnWin() is called.
  bool IsOnWinCalled() { return on_win_called_; }

 private:
  bool declare_defeat_called_ = false;
  bool stop_routing_pointer_events_called_ = false;
  bool on_win_called_ = false;
  a11y::GestureArena arena_;
  a11y::PointerEventRouter router_;
  a11y::GestureRecognizer* recognizer_;
};

}  // namespace accessibility_test
#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_ARENA_MEMBER_H_
