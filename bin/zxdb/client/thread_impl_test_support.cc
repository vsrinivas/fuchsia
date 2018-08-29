// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl_test_support.h"

#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

void ThreadImplTestSink::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  breakpoint_add_called_ = true;
  last_breakpoint_add_ = request;
  debug_ipc::MessageLoop::Current()->PostTask(
      [cb]() { cb(Err(), debug_ipc::AddOrChangeBreakpointReply()); });
}

void ThreadImplTestSink::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  breakpoint_remove_called_ = true;
  debug_ipc::MessageLoop::Current()->PostTask(
      [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
}

void ThreadImplTestSink::Backtrace(
    const debug_ipc::BacktraceRequest& request,
    std::function<void(const Err&, debug_ipc::BacktraceReply)> cb) {
  // Returns the canned response.
  debug_ipc::MessageLoop::Current()->PostTask([
    cb, response = frames_response_
  ]() { cb(Err(), std::move(response)); });
}

void ThreadImplTestSink::Resume(
    const debug_ipc::ResumeRequest& request,
    std::function<void(const Err&, debug_ipc::ResumeReply)> cb) {
  // Always returns success and then quits the message loop.
  debug_ipc::MessageLoop::Current()->PostTask([cb]() {
    cb(Err(), debug_ipc::ResumeReply());
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
}

ThreadImplTest::ThreadImplTest() = default;
ThreadImplTest::~ThreadImplTest() = default;

std::unique_ptr<RemoteAPI> ThreadImplTest::GetRemoteAPIImpl() {
  auto sink = std::make_unique<ThreadImplTestSink>();
  sink_ = sink.get();
  return std::move(sink);
}

TestThreadObserver::TestThreadObserver(Thread* thread) : thread_(thread) {
  thread->AddObserver(this);
}

TestThreadObserver::~TestThreadObserver() { thread_->RemoveObserver(this); }

void TestThreadObserver::OnThreadStopped(
    Thread* thread, debug_ipc::NotifyException::Type type,
    std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) {
  EXPECT_EQ(thread_, thread);
  got_stopped_ = true;
  hit_breakpoints_ = hit_breakpoints;
}

}  // namespace zxdb
