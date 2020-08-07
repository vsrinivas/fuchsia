// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/system.h"

#include <gtest/gtest.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

namespace {

struct ProcessInfo {
  uint64_t koid;
  std::string name;
};

class APISink : public MockRemoteAPI {
 public:
  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    attach_requests_.push_back(request);

    FX_DCHECK(info_count_ < next_infos_.size());
    auto info = next_infos_[info_count_++];

    debug_ipc::AttachReply reply;
    reply.status = debug_ipc::kZxOk;
    reply.koid = info.koid;
    reply.name = info.name;
    cb(Err(), std::move(reply));
  }

  const std::vector<debug_ipc::AttachRequest>& attach_requests() const { return attach_requests_; }

  // Sets the list of replys the sink will return.
  void SetNextInfos(std::vector<ProcessInfo> infos) {
    next_infos_ = std::move(infos);
    info_count_ = 0;
  }

 private:
  size_t info_count_ = 0;
  std::vector<ProcessInfo> next_infos_;
  std::vector<debug_ipc::AttachRequest> attach_requests_;
};

class SystemTest : public RemoteAPITest {
 public:
  SystemTest() = default;
  ~SystemTest() override = default;

  APISink* sink() { return sink_; }

 private:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<APISink>();
    // The session will own the sink.
    sink_ = sink.get();
    return sink;
  }

  APISink* sink_;
};

// We need a RAII-esque wrapper because observers are supposed to outlive the system.
class MockSystemObserver : public TargetObserver, public ProcessObserver {
 public:
  explicit MockSystemObserver(Session* session) : session_(session) {
    session->target_observers().AddObserver(this);
    session->process_observers().AddObserver(this);
  }
  ~MockSystemObserver() {
    session_->process_observers().RemoveObserver(this);
    session_->target_observers().RemoveObserver(this);
  }

  // TargetObserver.
  void DidCreateTarget(Target*) override { target_create_count_++; }

  // ProcessObserver.
  void DidCreateProcess(Process*, bool) override { process_create_count_++; }

  int target_create_count() const { return target_create_count_; }
  int process_create_count() const { return process_create_count_; }

 private:
  Session* session_ = nullptr;

  int target_create_count_ = 0;
  int process_create_count_ = 0;
};

}  // namespace

// Tests that thread state is updated when doing a system-wide continue.
TEST_F(SystemTest, GlobalContinue) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThread1Koid = 5678;
  Thread* thread1 = InjectThread(kProcessKoid, kThread1Koid);
  constexpr uint64_t kThread2Koid = 9012;
  Thread* thread2 = InjectThread(kProcessKoid, kThread2Koid);
  sink()->GetAndResetResumeCount();  // Clear from thread init.

  constexpr uint64_t kAddress = 0x12345678;
  constexpr uint64_t kStack = 0x7890;

  // Notify of thread stop on thread 1.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  break_notification.thread.process_koid = kProcessKoid;
  break_notification.thread.thread_koid = kThread1Koid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.emplace_back(kAddress, kStack, kStack);
  InjectException(break_notification);
  EXPECT_EQ(0, sink()->GetAndResetResumeCount());

  // Same on thread 2.
  break_notification.thread.thread_koid = kThread2Koid;
  InjectException(break_notification);

  // Continue globally. This should in turn update the thread.
  session().system().Continue(false);

  // Both threads should have been resumed in the backend.
  EXPECT_EQ(2, sink()->GetAndResetResumeCount());

  // The threads should have no stack.
  EXPECT_FALSE(thread1->GetStack().has_all_frames());
  ASSERT_EQ(0u, thread1->GetStack().size());
  EXPECT_FALSE(thread2->GetStack().has_all_frames());
  ASSERT_EQ(0u, thread2->GetStack().size());
}

TEST_F(SystemTest, FilterMatchesAndRematching) {
  System& system = session().system();
  MockSystemObserver system_observer(&session());

  constexpr uint64_t kJobKoid = 0x1234;
  Job job(&session(), false);
  job.AttachForTesting(kJobKoid, "job-name");
  ASSERT_TRUE(sink()->attach_requests().empty());

  // There should be only one empty target.
  auto targets = system.GetTargets();
  ASSERT_EQ(targets.size(), 1u);
  EXPECT_FALSE(targets[0]->GetProcess());

  // We match on a new process.
  constexpr uint64_t kProcessKoid = 0x5678;
  std::string kProcessName = "some-process";
  ProcessInfo info = {kProcessKoid, kProcessName};
  sink()->SetNextInfos({info});

  session().system().OnFilterMatches(&job, {kProcessKoid});

  // There should be an attach request.
  auto& requests = sink()->attach_requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].type, debug_ipc::TaskType::kProcess);
  EXPECT_EQ(requests[0].koid, kProcessKoid);

  // The system should've reused the empty target.
  ASSERT_EQ(system_observer.target_create_count(), 0);
  targets = system.GetTargets();
  ASSERT_EQ(targets.size(), 1u);

  // Should've created the process.
  ASSERT_EQ(system_observer.process_create_count(), 1);
  Process* process = targets[0]->GetProcess();
  ASSERT_TRUE(process);
  EXPECT_EQ(process->GetKoid(), kProcessKoid);
  EXPECT_EQ(process->GetName(), kProcessName);

  // Rematching should not create a new target.
  sink()->SetNextInfos({info});

  session().system().OnFilterMatches(&job, {kProcessKoid});

  // The system should've reused the empty target.
  ASSERT_EQ(system_observer.target_create_count(), 0);
  targets = system.GetTargets();
  ASSERT_EQ(targets.size(), 1u);

  // Should've created the process.
  ASSERT_EQ(system_observer.process_create_count(), 1);
  process = targets[0]->GetProcess();
  ASSERT_TRUE(process);
  EXPECT_EQ(process->GetKoid(), kProcessKoid);
  EXPECT_EQ(process->GetName(), kProcessName);
}

TEST_F(SystemTest, ExistenProcessShouldCreateTarget) {
  System& system = session().system();
  MockSystemObserver system_observer(&session());

  constexpr uint64_t kJobKoid = 0x1234;
  Job job(&session(), false);
  job.AttachForTesting(kJobKoid, "job-name");
  ASSERT_TRUE(sink()->attach_requests().empty());

  // Before injecting the process there should not be an event, after it there should be one.
  ASSERT_EQ(system_observer.process_create_count(), 0);
  constexpr uint64_t kProcessKoid1 = 0x1;
  InjectProcess(kProcessKoid1);
  ASSERT_EQ(system_observer.process_create_count(), 1);

  // There should be a target with a process.
  auto targets = system.GetTargets();
  ASSERT_EQ(targets.size(), 1u);
  Process* process = targets[0]->GetProcess();
  ASSERT_TRUE(process);
  ASSERT_EQ(process->GetKoid(), kProcessKoid1);

  // We match on a new process.
  constexpr uint64_t kProcessKoid2 = 0x2;
  std::string kProcessName = "some-process";
  ProcessInfo info = {kProcessKoid2, kProcessName};
  sink()->SetNextInfos({info});

  session().system().OnFilterMatches(&job, {kProcessKoid2});

  // There should be an attach request.
  auto& requests = sink()->attach_requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].type, debug_ipc::TaskType::kProcess);
  EXPECT_EQ(requests[0].koid, kProcessKoid2);

  // The system should've created a new target.
  ASSERT_EQ(system_observer.target_create_count(), 1);
  targets = system.GetTargets();
  ASSERT_EQ(targets.size(), 2u);

  // Should've created the process.
  ASSERT_EQ(system_observer.process_create_count(), 2);
  process = targets[1]->GetProcess();
  ASSERT_TRUE(process);
  EXPECT_EQ(process->GetKoid(), kProcessKoid2);
  EXPECT_EQ(process->GetName(), kProcessName);
}

}  // namespace zxdb
