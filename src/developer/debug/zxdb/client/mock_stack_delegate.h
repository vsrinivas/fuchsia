// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_STACK_DELEGATE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_STACK_DELEGATE_H_

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// A mock Stack::Delegate implementation that just passes through frames. You must call set_stack()
// after creating the Stack that uses this.
//
// Example:
//
//   MockStackDelegate delegate;
//   Stack stack(&delegate);
//   delegate.set_stack(&stack);
//
//   stack.SetFramesForTest(...);
//
class MockStackDelegate : public Stack::Delegate {
 public:
  void set_stack(Stack* s) { stack_ = s; }

  // Adds the given location to the list of things returned by GetSymbolizedLocationForStackFrame().
  void AddLocation(const Location& loc) { locations_[loc.address()] = loc; }

  // Sets the asynchronous resource to SyncFramesForStack(). Since this transfers ownership, it will
  // only affect the next call.
  void SetAsyncFrames(std::vector<std::unique_ptr<Frame>> frames) {
    async_frames_ = std::move(frames);
  }

  void SyncFramesForStack(fit::callback<void(const Err&)> cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb), this]() mutable {
      stack_->SetFramesForTest(std::move(async_frames_), true);
      cb(Err());
    });
  }

  std::unique_ptr<Frame> MakeFrameForStack(const debug_ipc::StackFrame& input,
                                           Location location) override {
    return std::make_unique<MockFrame>(nullptr, nullptr, location, input.sp);
  }

  Location GetSymbolizedLocationForStackFrame(const debug_ipc::StackFrame& input) override {
    auto found = locations_.find(input.ip);
    if (found == locations_.end())
      return Location(Location::State::kSymbolized, input.ip);
    return found->second;
  }

 private:
  Stack* stack_ = nullptr;
  std::map<uint64_t, Location> locations_;
  std::vector<std::unique_ptr<Frame>> async_frames_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_STACK_DELEGATE_H_
