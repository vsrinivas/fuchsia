// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_CONTEST_MEMBER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_CONTEST_MEMBER_H_

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"

namespace accessibility_test {

class MockContestMember {
 public:
  // Creates an |a11y::ContestMember| that fronts this mock. Only one may be active per mock.
  std::unique_ptr<a11y::ContestMember> TakeInterface();

  operator bool() const { return held_; }
  void SetStatus(a11y::ContestMember::Status status) { status_ = status; }
  // Sets the return value for |Accept|.
  void SetAccept(bool accept) { accept_ = accept; }
  bool IsAcceptCalled() const { return accept_called_; }
  bool IsRejectCalled() const { return reject_called_; }

 private:
  class ContestMember;

  bool held_ = false;
  bool accept_ = true;
  bool accept_called_ = false;
  bool reject_called_ = false;
  a11y::ContestMember::Status status_;
};

}  // namespace accessibility_test
#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_ARENA_TESTS_MOCKS_MOCK_CONTEST_MEMBER_H_
