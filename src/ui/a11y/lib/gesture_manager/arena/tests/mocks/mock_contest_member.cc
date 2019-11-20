// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_contest_member.h"

#include "src/lib/syslog/cpp/logger.h"

namespace accessibility_test {

class MockContestMember::ContestMember : public a11y::ContestMember {
 public:
  ContestMember(MockContestMember* state) : state_(state) { state_->held_ = true; }

  ~ContestMember() override { state_->held_ = false; }

  Status status() const override { return state_->status_; }

  bool Accept() override {
    state_->accept_called_ = true;
    return state_->accept_;
  };

  void Reject() override { state_->reject_called_ = true; }

 private:
  MockContestMember* const state_;
};

std::unique_ptr<a11y::ContestMember> MockContestMember::TakeInterface() {
  FX_CHECK(!held_);
  return std::make_unique<ContestMember>(this);
}

}  // namespace accessibility_test
