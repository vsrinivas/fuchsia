// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_controller_test.h"

#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

class ThreadControllerTest::ControllerTestSink : public RemoteAPI {
 public:
  // The argument is a variable to increment every time Resume() is called.
  explicit ControllerTestSink(ThreadControllerTest* test) : test_(test) {}

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    test_->resume_count_++;
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    test_->last_breakpoint_address_ = request.breakpoint.locations[0].address;
    test_->last_breakpoint_id_ = request.breakpoint.breakpoint_id;
    test_->add_breakpoint_count_++;
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    test_->remove_breakpoint_count_++;
  }

 private:
  ThreadControllerTest* test_;
};

ThreadControllerTest::ThreadControllerTest() = default;
ThreadControllerTest::~ThreadControllerTest() = default;

void ThreadControllerTest::SetUp() {
  RemoteAPITest::SetUp();
  process_ = InjectProcess(0x1234);
  thread_ = InjectThread(process_->GetKoid(), 0x7890);
}

std::unique_ptr<RemoteAPI> ThreadControllerTest::GetRemoteAPIImpl() {
  auto sink = std::make_unique<ControllerTestSink>(this);
  return sink;
}

}  // namespace zxdb
