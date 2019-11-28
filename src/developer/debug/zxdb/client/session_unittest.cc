// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/session.h"

#include <set>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job_context_impl.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class SessionTest;

class SessionSink : public RemoteAPI {
 public:
  explicit SessionSink(SessionTest* session_test) : session_test_(session_test) {}
  ~SessionSink() override = default;

  // Returns the breakpoint IDs that the backend is supposed to know about now.
  const std::set<uint32_t>& set_breakpoint_ids() const { return set_breakpoint_ids_; }

  // Returns the last received resume request sent by the client.
  const debug_ipc::ResumeRequest& resume_request() const { return resume_request_; }
  int resume_count() const { return resume_count_; }

  // Clears the last resume request and count.
  void ResetResumeState() {
    resume_request_ = debug_ipc::ResumeRequest();
    resume_count_ = 0;
  }

  void AppendProcessRecord(const debug_ipc::ProcessRecord& record) { records_.push_back(record); }

  // Adds all current system breakpoints to the exception notification. The calling code doesn't
  // have easy access to the backend IDs so this is where they come from. We assume the breakpoints
  // will all be hit at the same time (the tests will set them at the same address).
  void PopulateNotificationWithBreakpoints(debug_ipc::NotifyException* notify) {
    notify->hit_breakpoints.clear();
    for (uint32_t id : set_breakpoint_ids_) {
      notify->hit_breakpoints.emplace_back();
      notify->hit_breakpoints.back().id = id;
    }
  }

  void Resume(const debug_ipc::ResumeRequest& request,
              fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    resume_count_++;
    resume_request_ = request;
    MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(Err(), debug_ipc::ResumeReply()); });
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) override {
    set_breakpoint_ids_.insert(request.breakpoint.id);
    MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), debug_ipc::AddOrChangeBreakpointReply());
    });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) override {
    set_breakpoint_ids_.erase(request.breakpoint_id);
    MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), debug_ipc::RemoveBreakpointReply());
    });
  }

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {}

  void JobFilter(const debug_ipc::JobFilterRequest& request,
                 fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) override;

  void Status(const debug_ipc::StatusRequest& request,
              fit::callback<void(const Err&, debug_ipc::StatusReply)> cb) override;

 private:
  SessionTest* session_test_;
  debug_ipc::ResumeRequest resume_request_;
  int resume_count_ = 0;
  std::set<uint32_t> set_breakpoint_ids_;

  std::vector<debug_ipc::ProcessRecord> records_;
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

  void OnThreadStopped(Thread* thread, debug_ipc::ExceptionType type,
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
  const std::vector<std::pair<std::string, uint64_t>>& existing_processes() const {
    return existing_processes_;
  }

  void AddExistingProcess(const char* name, uint64_t koid) {
    existing_processes_.emplace_back(std::make_pair(std::string(name), koid));
  }

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
    auto sink = std::make_unique<SessionSink>(this);
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  SessionSink* sink_;  // Owned by the session.
  std::vector<std::pair<std::string, uint64_t>> existing_processes_;
};

void SessionSink::JobFilter(const debug_ipc::JobFilterRequest& request,
                            fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) {
  debug_ipc::JobFilterReply reply;
  std::vector<zxdb::Target*> targets = session_test_->session().system_impl().GetTargets();
  for (const auto& filter : request.filters) {
    for (const auto& process : session_test_->existing_processes()) {
      // Basic simulation of filtering. We only expect to find the filter string within the
      // process name.
      if (process.first.find(filter) != std::string::npos) {
        reply.matched_processes.emplace_back(process.second);
      }
    }
  }
  Err err;
  cb(err, reply);
}

void SessionSink::Status(const debug_ipc::StatusRequest& request,
                         fit::callback<void(const Err&, debug_ipc::StatusReply)> cb) {
  debug_ipc::StatusReply reply = {};
  reply.processes = records_;
  cb(Err(), std::move(reply));
}

}  // namespace

// This is a larger test that covers exception notifications in the Session
// object as well as breakpoint controllers and auto continuation. It sets up two breakpoints at the
// same address and reports them hit with various combinations of responses from each breakpoint.
TEST_F(SessionTest, MultiBreakpointStop) {
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);
  ASSERT_TRUE(process);

  // Thread with observer.
  constexpr uint64_t kThreadKoid = 5678;
  InjectThread(kProcessKoid, kThreadKoid);
  SessionThreadObserver thread_observer;
  session().thread_observers().AddObserver(&thread_observer);

  // An internal breakpoint.
  Breakpoint* bp_internal = session().system().CreateNewInternalBreakpoint();

  // The breakpoints are set at the same address.
  constexpr uint64_t kAddress = 0x12345678;
  BreakpointSettings bp_settings;
  bp_settings.enabled = true;
  bp_settings.locations.emplace_back(kAddress);
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
  notify.type = debug_ipc::ExceptionType::kSoftware;
  notify.thread.process_koid = kProcessKoid;
  notify.thread.thread_koid = kThreadKoid;
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  // Don't need stack pointers for this test.
  notify.thread.frames.emplace_back(kAddress, 0);
  sink()->PopulateNotificationWithBreakpoints(&notify);
  InjectException(notify);

  // The thread observer should be triggered since there is a regular user breakpoint responsible
  // for this address. It should be the only one in the notification (internal ones don't get listed
  // as a stop reason).
  ASSERT_EQ(1u, thread_observer.breakpoints().size());
  EXPECT_EQ(bp_user, thread_observer.breakpoints()[0]);

  // Delete the breakpoints, they should notify the backend.
  session().system().DeleteBreakpoint(bp_internal);
  session().system().DeleteBreakpoint(bp_user);
  EXPECT_TRUE(sink()->set_breakpoint_ids().empty());

  // Cleanup.
  session().thread_observers().RemoveObserver(&thread_observer);
}

// Tests that one shot breakpoints get deleted when the agent notifies us that the breakpoint was
// hit and deleted.
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
  settings.locations.emplace_back(kAddress);
  settings.one_shot = true;
  SyncSetSettings(bp, settings);

  // This will tell us if the breakpoint is deleted.
  fxl::WeakPtr<Breakpoint> weak_bp = bp->GetWeakPtr();

  debug_ipc::NotifyException notify;
  notify.type = debug_ipc::ExceptionType::kSoftware;
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

TEST_F(SessionTest, FilterExistingProcesses) {
  constexpr uint64_t kJobKoid = 3333;
  constexpr uint64_t kProcessKoid1 = 1111;
  constexpr uint64_t kProcessKoid2 = 2222;

  JobContextImpl job(&session().system_impl(), false);
  job.AddJobImplForTesting(kJobKoid, "job-name");

  AddExistingProcess("test_1", kProcessKoid1);
  AddExistingProcess("test_2", kProcessKoid2);

  // First, we should have only one unused target.
  ASSERT_EQ(session().system().GetTargets().size(), 1UL);
  ASSERT_EQ(session().system().GetTargets()[0]->GetState(), zxdb::Target::State::kNone);

  Filter* filter = session().system().CreateNewFilter();
  filter->SetPattern("test");
  filter->SetJob(session().system().GetJobContexts()[0]);

  loop().RunUntilNoTasks();

  // If both existing processes have been detected, we should have two used targets.
  ASSERT_EQ(session().system().GetTargets().size(), 2UL);
  ASSERT_NE(session().system().GetTargets()[0]->GetState(), zxdb::Target::State::kNone);
  ASSERT_NE(session().system().GetTargets()[1]->GetState(), zxdb::Target::State::kNone);
}

TEST_F(SessionTest, StatusRequest) {
  constexpr uint64_t kProcessKoid1 = 1;
  constexpr uint64_t kProcessKoid2 = 2;
  const std::string kProcessName1 = "process-1";
  const std::string kProcessName2 = "process-2";

  sink()->AppendProcessRecord({kProcessKoid1, kProcessName1, {}});
  sink()->AppendProcessRecord({kProcessKoid2, kProcessName2, {}});

  bool called = false;
  debug_ipc::StatusReply status = {};
  sink()->Status({}, [&called, &status](const Err& err, debug_ipc::StatusReply reply) {
    called = true;
    status = std::move(reply);
  });

  ASSERT_TRUE(called);
  ASSERT_EQ(status.processes.size(), 2u);
  EXPECT_EQ(status.processes[0].process_koid, kProcessKoid1);
  EXPECT_EQ(status.processes[0].process_name, kProcessName1);
  EXPECT_EQ(status.processes[1].process_koid, kProcessKoid2);
  EXPECT_EQ(status.processes[1].process_name, kProcessName2);
}

}  // namespace zxdb
