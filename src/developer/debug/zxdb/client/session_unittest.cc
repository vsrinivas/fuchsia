// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/session.h"

#include <set>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class SessionSink : public RemoteAPI {
 public:
  SessionSink() = default;
  ~SessionSink() override = default;

  // Returns the breakpoint IDs that the backend is supposed to know about now.
  const std::set<uint32_t>& set_breakpoint_ids() const {
    return set_breakpoint_ids_;
  }

  // Returns the last received resume request sent by the client.
  const debug_ipc::ResumeRequest& resume_request() const {
    return resume_request_;
  }
  int resume_count() const { return resume_count_; }

  // Clears the last resume request and count.
  void ResetResumeState() {
    resume_request_ = debug_ipc::ResumeRequest();
    resume_count_ = 0;
  }

  // Adds all current system breakpoints to the exception notification. The
  // calling code doesn't have easy access to the backend IDs so this is where
  // they come from. We assume the breakpoints will all be hit at the same time
  // (the tests will set them at the same address).
  void PopulateNotificationWithBreakpoints(debug_ipc::NotifyException* notify) {
    notify->hit_breakpoints.clear();
    for (uint32_t id : set_breakpoint_ids_) {
      notify->hit_breakpoints.emplace_back();
      notify->hit_breakpoints.back().id = id;
    }
  }

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    resume_count_++;
    resume_request_ = request;
    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err(), debug_ipc::ResumeReply()); });
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    set_breakpoint_ids_.insert(request.breakpoint.id);
    MessageLoop::Current()->PostTask(FROM_HERE, [cb]() {
      cb(Err(), debug_ipc::AddOrChangeBreakpointReply());
    });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    set_breakpoint_ids_.erase(request.breakpoint_id);
    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
  }

 private:
  debug_ipc::ResumeRequest resume_request_;
  int resume_count_ = 0;
  std::set<uint32_t> set_breakpoint_ids_;
};

class SessionThreadObserver : public ThreadObserver {
 public:
  void ResetStopState() {
    stop_count_ = 0;
    breakpoints_.clear();
  }

  int stop_count() const { return stop_count_; }

  // The breakpoints that triggered from the last stop notification.
  const std::vector<Breakpoint*> breakpoints() const { return breakpoints_; }

  void OnThreadStopped(
      Thread* thread, debug_ipc::NotifyException::Type type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
    stop_count_++;
    breakpoints_.clear();
    for (auto& bp : hit_breakpoints) {
      if (bp)
        breakpoints_.push_back(bp.get());
    }
  }

 private:
  int stop_count_ = 0;
  std::vector<Breakpoint*> breakpoints_;
};

class SessionTest : public RemoteAPITest {
 public:
  SessionTest() = default;
  ~SessionTest() override = default;

  SessionSink* sink() { return sink_; }

  Err SyncSetSettings(Breakpoint* bp, const BreakpointSettings& settings) {
    Err out_err;
    bp->SetSettings(settings, [&out_err](const Err& new_err) {
      out_err = new_err;
      MessageLoop::Current()->QuitNow();
    });
    loop().Run();
    return out_err;
  }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<SessionSink>();
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  SessionSink* sink_;  // Owned by the session.
};

}  // namespace

// This is a larger test that covers exception notifications in the Session
// object as well as breakpoint controllers and auto continuation. It sets up
// two breakpoints at the same address and reports them hit with various
// combinations of responses from each breakpoint.
TEST_F(SessionTest, MultiBreakpointStop) {
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);
  ASSERT_TRUE(process);

  // Thread with observer.
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);
  SessionThreadObserver thread_observer;
  thread->AddObserver(&thread_observer);

  // An internal breakpoint.
  Breakpoint* bp_internal = session().system().CreateNewInternalBreakpoint();

  // The breakpoints are set at the same address.
  constexpr uint64_t kAddress = 0x12345678;
  BreakpointSettings bp_settings;
  bp_settings.enabled = true;
  bp_settings.location = InputLocation(kAddress);
  SyncSetSettings(bp_internal, bp_settings);

  // Should have gotten the breakpoint registering itself.
  ASSERT_EQ(1u, sink()->set_breakpoint_ids().size());

  // Now make a user breakpoint at the same place, it should have registered.
  Breakpoint* bp_user = session().system().CreateNewBreakpoint();
  SyncSetSettings(bp_user, bp_settings);
  ASSERT_EQ(2u, sink()->set_breakpoint_ids().size());

  // Do a notification with both breakpoints getting hit.
  sink()->ResetResumeState();
  thread_observer.ResetStopState();
  debug_ipc::NotifyException notify;
  notify.type = debug_ipc::NotifyException::Type::kSoftware;
  notify.thread.process_koid = kProcessKoid;
  notify.thread.thread_koid = kThreadKoid;
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  // Don't need stack pointers for this test.
  notify.thread.frames.emplace_back(kAddress, 0);
  sink()->PopulateNotificationWithBreakpoints(&notify);
  InjectException(notify);

  // The thread observer should be triggered since there is a regular user
  // breakpoint responsible for this address. It should be the only one in the
  // notification (internal ones don't get listed as a stop reason).
  ASSERT_EQ(1u, thread_observer.breakpoints().size());
  EXPECT_EQ(bp_user, thread_observer.breakpoints()[0]);

  // Delete the breakpoints, they should notify the backend.
  session().system().DeleteBreakpoint(bp_internal);
  session().system().DeleteBreakpoint(bp_user);
  EXPECT_TRUE(sink()->set_breakpoint_ids().empty());

  // Cleanup.
  thread->RemoveObserver(&thread_observer);
}

// Tests that one shot breakpoints get deleted when the agent notifies us that
// the breakpoint was hit and deleted.
TEST_F(SessionTest, OneShotBreakpointDelete) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  InjectThread(kProcessKoid, kThreadKoid);

  // Create a breakpoint.
  Breakpoint* bp = session().system().CreateNewBreakpoint();
  constexpr uint64_t kAddress = 0x12345678;
  BreakpointSettings settings;
  settings.enabled = true;
  settings.location = InputLocation(kAddress);
  settings.one_shot = true;
  SyncSetSettings(bp, settings);

  // This will tell us if the breakpoint is deleted.
  fxl::WeakPtr<Breakpoint> weak_bp = bp->GetWeakPtr();

  debug_ipc::NotifyException notify;
  notify.type = debug_ipc::NotifyException::Type::kSoftware;
  notify.thread.process_koid = kProcessKoid;
  notify.thread.thread_koid = kThreadKoid;
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  // Don't need stack pointers for this test.
  notify.thread.frames.emplace_back(kAddress, 0);
  sink()->PopulateNotificationWithBreakpoints(&notify);

  // There should have been one breakpoint populated, mark deleted.
  ASSERT_EQ(1u, notify.hit_breakpoints.size());
  notify.hit_breakpoints[0].should_delete = true;

  // Notify of the breakpoint hit and delete. It should be deleted.
  EXPECT_TRUE(weak_bp);
  InjectException(notify);
  EXPECT_FALSE(weak_bp);
}

}  // namespace zxdb
