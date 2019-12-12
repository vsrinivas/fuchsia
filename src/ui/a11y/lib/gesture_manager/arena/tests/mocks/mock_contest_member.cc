// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"

namespace accessibility_test {

class MockContestMember::ContestMember : public a11y::ContestMember {
 public:
  ContestMember(MockContestMember* state) : state_(state) {
    state_->held_ = state_->owned_ = true;
    state_->status_ = Status::kUndecided;
  }
  ~ContestMember() override {
    if (state_->status_ == Status::kUndecided) {
      state_->status_ = Status::kRejected;
    }

    state_->held_ = state_->owned_ = false;
  }

  // While repeated calls to |Accept| or |Reject| no-op, changing state is probably not a good sign
  // and makes test verification less definitive, so surface them as test failures.

  void Accept() override {
    EXPECT_NE(state_->status_, Status::kRejected);
    state_->status_ = Status::kAccepted;
  }

  void Reject() override {
    EXPECT_NE(state_->status_, Status::kAccepted);
    state_->status_ = Status::kRejected;
    state_->held_ = false;
  }

 private:
  MockContestMember* const state_;
};

std::unique_ptr<a11y::ContestMember> MockContestMember::TakeInterface() {
  FX_CHECK(!owned_);
  return std::make_unique<ContestMember>(this);
}

}  // namespace accessibility_test
