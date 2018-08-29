// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This file contains a test harness and helper classes for writing tests
// involving lower-level thread contol such as ThreadImpl utself, and
// ThreadControllers.

#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/thread_observer.h"

namespace zxdb {

class ThreadImplTestSink : public RemoteAPI {
 public:
  void set_frames_response(const debug_ipc::BacktraceReply& response) {
    frames_response_ = response;
  }

  bool breakpoint_add_called() const { return breakpoint_add_called_; }
  bool breakpoint_remove_called() const { return breakpoint_remove_called_; }

  const debug_ipc::AddOrChangeBreakpointRequest& last_breakpoint_add() const {
    return last_breakpoint_add_;
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override;
  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override;
  void Backtrace(
      const debug_ipc::BacktraceRequest& request,
      std::function<void(const Err&, debug_ipc::BacktraceReply)> cb) override;
  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override;

 private:
  debug_ipc::BacktraceReply frames_response_;

  bool breakpoint_add_called_ = false;
  debug_ipc::AddOrChangeBreakpointRequest last_breakpoint_add_;

  bool breakpoint_remove_called_ = false;
};

class ThreadImplTest : public RemoteAPITest {
 public:
  ThreadImplTest();
  ~ThreadImplTest() override;

  ThreadImplTestSink& sink() { return *sink_; }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override;

 private:
  ThreadImplTestSink* sink_;  // Owned by the session.
};

class TestThreadObserver : public ThreadObserver {
 public:
  explicit TestThreadObserver(Thread* thread);
  ~TestThreadObserver();

  bool got_stopped() const { return got_stopped_; }
  const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints() {
    return hit_breakpoints_;
  }

  void OnThreadStopped(
      Thread* thread, debug_ipc::NotifyException::Type type,
      std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) override;

 private:
  Thread* thread_;

  bool got_stopped_ = false;
  std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints_;
};

}  // namespace zxdb
