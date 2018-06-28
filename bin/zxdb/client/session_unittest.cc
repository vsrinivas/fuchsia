// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_controller.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "gtest/gtest.h"

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
      notify->hit_breakpoints.back().breakpoint_id = id;
    }
  }

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    resume_count_++;
    resume_request_ = request;
    MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::ResumeReply()); });
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    set_breakpoint_ids_.insert(request.breakpoint.breakpoint_id);
    MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::AddOrChangeBreakpointReply()); });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    set_breakpoint_ids_.erase(request.breakpoint_id);
    MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
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

  void OnThreadStopped(Thread* thread, debug_ipc::NotifyException::Type type,
                       std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) {
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

// Returns a defined action to the breakpoint. It also expects that the
// thread parameter that that thread's state matches a predefined value.
class TestBreakpointController : public BreakpointController {
 public:
  void set_action(BreakpointAction action) { action_ = action; }
  void set_expected_thread(Thread* thread) { expected_thread_ = thread; }
  void set_expected_thread_state(debug_ipc::ThreadRecord::State s) {
    expected_thread_state_ = s;
  }

  BreakpointAction GetBreakpointHitAction(Breakpoint* bp,
                                          Thread* thread) override {
    EXPECT_EQ(expected_thread_, thread);
    EXPECT_EQ(expected_thread_state_, thread->GetState());
    return action_;
  }

 private:
  BreakpointAction action_ = BreakpointAction::kStop;
  Thread* expected_thread_ = nullptr;
  debug_ipc::ThreadRecord::State expected_thread_state_ =
      debug_ipc::ThreadRecord::State::kBlocked;
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

  // Two internal breakpoints and controllers.
  TestBreakpointController cntl1;
  Breakpoint* bp1 = session().system().CreateNewInternalBreakpoint(&cntl1);
  TestBreakpointController cntl2;
  Breakpoint* bp2 = session().system().CreateNewInternalBreakpoint(&cntl2);

  // The breakpoints are set at the same address.
  constexpr uint64_t kAddress = 0x12345678;
  BreakpointSettings bp_settings;
  bp_settings.enabled = true;
  bp_settings.location = InputLocation(kAddress);
  SyncSetSettings(bp1, bp_settings);
  SyncSetSettings(bp2, bp_settings);

  // Should have gottten both breakpoints registering themselves.
  ASSERT_EQ(2u, sink()->set_breakpoint_ids().size());

  // Make both breakpoints report "continue" when they're hit.
  cntl1.set_action(BreakpointAction::kContinue);
  cntl1.set_expected_thread(thread);
  cntl1.set_expected_thread_state(debug_ipc::ThreadRecord::State::kBlocked);
  cntl2.set_action(BreakpointAction::kContinue);
  cntl2.set_expected_thread(thread);
  cntl2.set_expected_thread_state(debug_ipc::ThreadRecord::State::kBlocked);

  // Notify of a breakpoint hit at both breakpoints.
  debug_ipc::NotifyException notify;
  notify.process_koid = kProcessKoid;
  notify.type = debug_ipc::NotifyException::Type::kSoftware;
  notify.thread.koid = kThreadKoid;
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  sink()->PopulateNotificationWithBreakpoints(&notify);
  InjectException(notify);

  // Since both breakpoints requested "continue", it should have sent back a
  // resume request and not reported a thread stop.
  ASSERT_EQ(1, sink()->resume_count());
  EXPECT_EQ(kProcessKoid, sink()->resume_request().process_koid);
  EXPECT_EQ(kThreadKoid, sink()->resume_request().thread_koid);
  EXPECT_EQ(0, thread_observer.stop_count());

  // Do a second request, this time where one requests a silent stop.
  sink()->ResetResumeState();
  thread_observer.ResetStopState();
  cntl2.set_action(BreakpointAction::kSilentStop);
  InjectException(notify);

  // The silent stop should take precedence so the thread should not be
  // resumed, but the thread stop notification should still not be sent.
  EXPECT_EQ(0, sink()->resume_count());
  EXPECT_EQ(0, thread_observer.stop_count());

  // Do a third request, this time with a full stop.
  sink()->ResetResumeState();
  thread_observer.ResetStopState();
  cntl2.set_action(BreakpointAction::kStop);
  InjectException(notify);

  // The full stop should take precedence, the thread should have issued a
  // notification. Since both breakpoints are internal, no breakpoints should
  // be reported in the callback.
  EXPECT_EQ(0, sink()->resume_count());
  EXPECT_EQ(1, thread_observer.stop_count());

  // Now make a user breakpoint at the same place, it should have registered.
  Breakpoint* bp_user = session().system().CreateNewBreakpoint();
  SyncSetSettings(bp_user, bp_settings);
  ASSERT_EQ(3u, sink()->set_breakpoint_ids().size());

  // Do a notification with all three breakpoints getting hit.
  sink()->ResetResumeState();
  thread_observer.ResetStopState();
  sink()->PopulateNotificationWithBreakpoints(&notify);
  InjectException(notify);

  // The current state:
  //   bp1: kContinue (internal)
  //   bp2: kStop (internal)
  //   bp_user: kStop (non-internal)
  // The thread observer should be triggered since there is a regular user
  // breakpoint responsible for this address. It should be the only one in the
  // notification (internal ones don't get listed as a stop reason).
  ASSERT_EQ(1u, thread_observer.breakpoints().size());
  EXPECT_EQ(bp_user, thread_observer.breakpoints()[0]);

  // Delete the breakpoints, they should notify the backend.
  session().system().DeleteBreakpoint(bp1);
  session().system().DeleteBreakpoint(bp2);
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
  notify.process_koid = kProcessKoid;
  notify.type = debug_ipc::NotifyException::Type::kSoftware;
  notify.thread.koid = kThreadKoid;
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
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
