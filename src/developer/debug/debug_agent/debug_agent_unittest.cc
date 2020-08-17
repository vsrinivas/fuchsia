// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/mock_debug_agent_harness.h"
#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"
#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/test_with_loop.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace fuchsia::exception;

namespace debug_agent {
namespace {

bool HasAttachedProcessWithKoid(DebugAgent* debug_agent, zx_koid_t koid) {
  DebuggedProcess* proc = debug_agent->GetDebuggedProcess(koid);
  if (!proc)
    return false;

  // All of our process handles should be mock ones.
  return static_cast<MockProcessHandle&>(proc->process_handle()).is_attached();
}

// Setup -------------------------------------------------------------------------------------------

class DebugAgentMockProcess : public MockProcess {
 public:
  DebugAgentMockProcess(DebugAgent* debug_agent, zx_koid_t koid, std::string name)
      : MockProcess(debug_agent, koid, std::move(name)) {}

  ~DebugAgentMockProcess() = default;

  void SuspendAndSendModulesIfKnown() override {
    // Send the modules over to the ipc.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyModules(modules_to_send_, &writer);
    debug_agent()->stream()->Write(writer.MessageComplete());
  };

  void set_modules_to_send(debug_ipc::NotifyModules m) { modules_to_send_ = std::move(m); }

 private:
  debug_ipc::NotifyModules modules_to_send_;
};

}  // namespace

// Tests -------------------------------------------------------------------------------------------

class DebugAgentTests : public debug_ipc::TestWithLoop {};

TEST_F(DebugAgentTests, OnGlobalStatus) {
  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  debug_ipc::StatusRequest request = {};

  debug_ipc::StatusReply reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.processes.size(), 0u);

  constexpr uint64_t kProcessKoid1 = 0x1234;
  const std::string kProcessName1 = "process-1";
  constexpr uint64_t kProcess1ThreadKoid1 = 0x1;

  auto process1 = std::make_unique<MockProcess>(nullptr, kProcessKoid1, kProcessName1);
  process1->AddThread(kProcess1ThreadKoid1);
  harness.debug_agent()->InjectProcessForTest(std::move(process1));

  reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.processes.size(), 1u);
  EXPECT_EQ(reply.processes[0].process_koid, kProcessKoid1);
  EXPECT_EQ(reply.processes[0].process_name, kProcessName1);
  ASSERT_EQ(reply.processes[0].threads.size(), 1u);
  EXPECT_EQ(reply.processes[0].threads[0].process_koid, kProcessKoid1);
  EXPECT_EQ(reply.processes[0].threads[0].thread_koid, kProcess1ThreadKoid1);

  constexpr uint64_t kProcessKoid2 = 0x5678;
  const std::string kProcessName2 = "process-2";
  constexpr uint64_t kProcess2ThreadKoid1 = 0x1;
  constexpr uint64_t kProcess2ThreadKoid2 = 0x2;

  auto process2 = std::make_unique<MockProcess>(nullptr, kProcessKoid2, kProcessName2);
  process2->AddThread(kProcess2ThreadKoid1);
  process2->AddThread(kProcess2ThreadKoid2);
  harness.debug_agent()->InjectProcessForTest(std::move(process2));

  reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.processes.size(), 2u);
  EXPECT_EQ(reply.processes[0].process_koid, kProcessKoid1);
  EXPECT_EQ(reply.processes[0].process_name, kProcessName1);
  ASSERT_EQ(reply.processes[0].threads.size(), 1u);
  EXPECT_EQ(reply.processes[0].threads[0].process_koid, kProcessKoid1);
  EXPECT_EQ(reply.processes[0].threads[0].thread_koid, kProcess1ThreadKoid1);

  EXPECT_EQ(reply.processes[1].process_koid, kProcessKoid2);
  EXPECT_EQ(reply.processes[1].process_name, kProcessName2);
  ASSERT_EQ(reply.processes[1].threads.size(), 2u);
  EXPECT_EQ(reply.processes[1].threads[0].process_koid, kProcessKoid2);
  EXPECT_EQ(reply.processes[1].threads[0].thread_koid, kProcess2ThreadKoid1);
  EXPECT_EQ(reply.processes[1].threads[1].process_koid, kProcessKoid2);
  EXPECT_EQ(reply.processes[1].threads[1].thread_koid, kProcess2ThreadKoid2);

  // Set a limbo provider.

  constexpr zx_koid_t kProcKoid1 = 100;
  constexpr zx_koid_t kThreadKoid1 = 101;
  harness.system_interface()->mock_limbo_provider().AppendException(
      MockProcessHandle(kProcKoid1, "proc1"), MockThreadHandle(kThreadKoid1, "thread1"),
      MockExceptionHandle(kThreadKoid1));

  constexpr zx_koid_t kProcKoid2 = 102;
  constexpr zx_koid_t kThreadKoid2 = 103;
  harness.system_interface()->mock_limbo_provider().AppendException(
      MockProcessHandle(kProcKoid2, "proc2"), MockThreadHandle(kThreadKoid1, "thread2"),
      MockExceptionHandle(kThreadKoid2));

  reply = {};
  remote_api->OnStatus(request, &reply);

  // The attached processes should still be there.
  ASSERT_EQ(reply.processes.size(), 2u);

  // The limbo processes should be there.
  ASSERT_EQ(reply.limbo.size(), 2u);
  EXPECT_EQ(reply.limbo[0].process_koid, kProcKoid1);
  EXPECT_EQ(reply.limbo[0].process_name, "proc1");
  ASSERT_EQ(reply.limbo[0].threads.size(), 1u);

  // TODO(donosoc): Add exception type.
}

TEST_F(DebugAgentTests, OnProcessStatus) {
  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  constexpr uint64_t kProcessKoid1 = 0x1234;
  std::string kProcessName1 = "process-1";
  auto process1 =
      std::make_unique<DebugAgentMockProcess>(harness.debug_agent(), kProcessKoid1, kProcessName1);
  harness.debug_agent()->InjectProcessForTest(std::move(process1));

  constexpr uint64_t kProcessKoid2 = 0x5678;
  std::string kProcessName2 = "process-2";
  auto process2 =
      std::make_unique<DebugAgentMockProcess>(harness.debug_agent(), kProcessKoid2, kProcessName2);
  auto* process2_ptr = process2.get();
  harness.debug_agent()->InjectProcessForTest(std::move(process2));

  // Asking for a un-existent process should fail.
  debug_ipc::ProcessStatusRequest request = {};
  request.process_koid = 0xdeadbeef;

  debug_ipc::ProcessStatusReply reply = {};
  remote_api->OnProcessStatus(request, &reply);
  EXPECT_EQ(reply.status, (uint32_t)ZX_ERR_NOT_FOUND) << zx_status_get_string(reply.status);

  debug_ipc::NotifyModules modules_to_send = {};
  modules_to_send.process_koid = kProcessKoid2;
  modules_to_send.modules.push_back({"module-1", 0x1, 0x5, "build-1"});
  modules_to_send.modules.push_back({"module-2", 0x2, 0x7, "build-2"});
  process2_ptr->set_modules_to_send(modules_to_send);

  // Asking for an existent one should send the process and modules notification.
  request.process_koid = kProcessKoid2;
  remote_api->OnProcessStatus(request, &reply);
  EXPECT_EQ(reply.status, (uint32_t)ZX_OK) << zx_status_get_string(reply.status);

  loop().RunUntilNoTasks();

  auto& process_starts = harness.stream_backend()->process_starts();
  ASSERT_EQ(process_starts.size(), 1u);
  EXPECT_EQ(process_starts[0].koid, kProcessKoid2);
  EXPECT_EQ(process_starts[0].name, kProcessName2);

  auto& modules = harness.stream_backend()->modules();
  ASSERT_EQ(modules.size(), 1u);
  EXPECT_EQ(modules[0].process_koid, kProcessKoid2);

  ASSERT_EQ(modules[0].modules.size(), modules_to_send.modules.size());
  ASSERT_EQ(modules[0].modules[0].name, modules_to_send.modules[0].name);
  ASSERT_EQ(modules[0].modules[0].base, modules_to_send.modules[0].base);
  ASSERT_EQ(modules[0].modules[0].debug_address, modules_to_send.modules[0].debug_address);
  ASSERT_EQ(modules[0].modules[0].build_id, modules_to_send.modules[0].build_id);
  ASSERT_EQ(modules[0].modules[1].name, modules_to_send.modules[1].name);
  ASSERT_EQ(modules[0].modules[1].base, modules_to_send.modules[1].base);
  ASSERT_EQ(modules[0].modules[1].debug_address, modules_to_send.modules[1].debug_address);
  ASSERT_EQ(modules[0].modules[1].build_id, modules_to_send.modules[1].build_id);
}

TEST_F(DebugAgentTests, OnAttachNotFound) {
  uint32_t transaction_id = 1u;

  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  debug_ipc::AttachRequest attach_request;
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = -1;

  remote_api->OnAttach(transaction_id++, attach_request);

  {
    // Should've gotten an attach reply.
    auto& attach_replies = harness.stream_backend()->attach_replies();
    ASSERT_EQ(attach_replies.size(), 1u);
    EXPECT_ZX_EQ(attach_replies[0].status, ZX_ERR_NOT_FOUND);
  }

  constexpr zx_koid_t kProcKoid1 = 100;
  constexpr zx_koid_t kThreadKoid1 = 101;
  harness.system_interface()->mock_limbo_provider().AppendException(
      MockProcessHandle(kProcKoid1, "proc1"), MockThreadHandle(kThreadKoid1, "thread1"),
      MockExceptionHandle(kThreadKoid1));

  // Even with limbo it should fail.
  remote_api->OnAttach(transaction_id++, attach_request);

  {
    // Should've gotten an attach reply.
    auto& attach_replies = harness.stream_backend()->attach_replies();
    ASSERT_EQ(attach_replies.size(), 2u);
    EXPECT_ZX_EQ(attach_replies[1].status, ZX_ERR_NOT_FOUND);
  }
}

TEST_F(DebugAgentTests, OnAttach) {
  constexpr zx_koid_t kProcess1Koid = 11u;  // Koid for job1-p2 from the GetMockJobTree() hierarchy.
  uint32_t transaction_id = 1u;

  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  debug_ipc::AttachRequest attach_request;
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = kProcess1Koid;

  remote_api->OnAttach(transaction_id++, attach_request);

  // We should've received a watch command (which does the low level exception watching).
  EXPECT_TRUE(HasAttachedProcessWithKoid(harness.debug_agent(), kProcess1Koid));

  // We should've gotten an attach reply.
  auto& attach_replies = harness.stream_backend()->attach_replies();
  auto reply = attach_replies.back();
  ASSERT_EQ(attach_replies.size(), 1u);
  EXPECT_ZX_EQ(reply.status, ZX_OK);
  EXPECT_EQ(reply.koid, kProcess1Koid);
  EXPECT_EQ(reply.name, "job1-p2");

  // Asking for some invalid process should fail.
  attach_request.koid = 0x231315;  // Some invalid value.
  remote_api->OnAttach(transaction_id++, attach_request);

  // We should've gotten an error reply.
  ASSERT_EQ(attach_replies.size(), 2u);
  reply = attach_replies.back();
  EXPECT_ZX_EQ(reply.status, ZX_ERR_NOT_FOUND);

  // Attaching to a third process should work.
  attach_request.koid = 21u;
  remote_api->OnAttach(transaction_id++, attach_request);

  ASSERT_EQ(attach_replies.size(), 3u);
  reply = attach_replies.back();
  EXPECT_ZX_EQ(reply.status, ZX_OK);
  EXPECT_EQ(reply.koid, 21u);
  EXPECT_EQ(reply.name, "job121-p2");

  // Attaching again to a process should fail.
  remote_api->OnAttach(transaction_id++, attach_request);

  ASSERT_EQ(attach_replies.size(), 4u);
  reply = attach_replies.back();
  EXPECT_ZX_EQ(reply.status, ZX_ERR_ALREADY_BOUND);
}

TEST_F(DebugAgentTests, AttachToLimbo) {
  // debug_ipc::SetDebugMode(true);
  // debug_ipc::SetLogCategories({debug_ipc::LogCategory::kAll});
  uint32_t transaction_id = 1u;

  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  constexpr zx_koid_t kProcKoid = 100;
  constexpr zx_koid_t kThreadKoid = 101;
  MockProcessHandle mock_process(kProcKoid, "proc");
  MockThreadHandle mock_thread(kThreadKoid, "thread");
  mock_process.set_threads({mock_thread});

  harness.system_interface()->mock_limbo_provider().AppendException(
      mock_process, mock_thread, MockExceptionHandle(kThreadKoid));

  debug_ipc::AttachRequest attach_request = {};
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = kProcKoid;
  remote_api->OnAttach(transaction_id++, attach_request);

  // Process should be watching.
  EXPECT_TRUE(HasAttachedProcessWithKoid(harness.debug_agent(), kProcKoid));

  // We should've gotten an attach reply.
  auto& attach_replies = harness.stream_backend()->attach_replies();
  ASSERT_EQ(attach_replies.size(), 1u);
  auto reply = attach_replies.back();
  EXPECT_ZX_EQ(reply.status, ZX_OK);
  EXPECT_EQ(reply.koid, kProcKoid);
  EXPECT_EQ(reply.name, "proc");

  {
    DebuggedProcess* process = harness.debug_agent()->GetDebuggedProcess(kProcKoid);
    ASSERT_TRUE(process);
    auto threads = process->GetThreads();
    ASSERT_EQ(threads.size(), 1u);

    // Search for the exception thread.
    DebuggedThread* exception_thread = nullptr;
    for (DebuggedThread* thread : threads) {
      if (thread->koid() == kThreadKoid) {
        exception_thread = thread;
        break;
      }
    }

    ASSERT_TRUE(exception_thread);
    ASSERT_TRUE(exception_thread->in_exception());
    EXPECT_EQ(exception_thread->exception_handle()->GetThreadHandle()->GetKoid(), kThreadKoid);
  }
}

TEST_F(DebugAgentTests, OnEnterLimbo) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid1 = 100;
  constexpr zx_koid_t kThreadKoid1 = 101;
  harness.system_interface()->mock_limbo_provider().AppendException(
      MockProcessHandle(kProcKoid1, "proc1"), MockThreadHandle(kThreadKoid1, "thread1"),
      MockExceptionHandle(kThreadKoid1));

  // Call the limbo.
  harness.system_interface()->mock_limbo_provider().CallOnEnterLimbo();

  // Should've sent a notification.
  {
    ASSERT_EQ(harness.stream_backend()->process_starts().size(), 1u);

    auto& process_start = harness.stream_backend()->process_starts()[0];
    EXPECT_EQ(process_start.type, debug_ipc::NotifyProcessStarting::Type::kLimbo);
    EXPECT_EQ(process_start.koid, kProcKoid1);
    EXPECT_EQ(process_start.component_id, 0u);
    EXPECT_EQ(process_start.name, "proc1");
  }
}

TEST_F(DebugAgentTests, DetachFromLimbo) {
  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  constexpr zx_koid_t kProcKoid = 14;  // MockJobTree job11-p1

  // Attempting to detach to a process that doesn't exist should fail.
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = kProcKoid;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_ERR_NOT_FOUND);
    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 0u);
  }

  // Adding it should now find it and remove it.
  constexpr zx_koid_t kProcKoid1 = 100;
  constexpr zx_koid_t kThreadKoid1 = 101;
  harness.system_interface()->mock_limbo_provider().AppendException(
      MockProcessHandle(kProcKoid1, "proc1"), MockThreadHandle(kThreadKoid1, "thread1"),
      MockExceptionHandle(kThreadKoid1));
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = kProcKoid1;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_OK);
    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 1u);
    EXPECT_EQ(harness.system_interface()->mock_limbo_provider().release_calls()[0], kProcKoid1);
  }

  // This should've remove it from limbo, trying it again should fail.
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = kProcKoid1;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_ERR_NOT_FOUND);
    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 1u);
    EXPECT_EQ(harness.system_interface()->mock_limbo_provider().release_calls()[0], kProcKoid1);
  }
}

TEST_F(DebugAgentTests, Kill) {
  uint32_t transaction_id = 1u;

  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  constexpr zx_koid_t kProcKoid = 14;  // MockJobTree job11-p1

  // Attempt to kill a process that's not there should fail.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = kProcKoid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  // Attach to a process so that the debugger knows about it.
  {
    debug_ipc::AttachRequest attach_request = {};
    attach_request.type = debug_ipc::TaskType::kProcess;
    attach_request.koid = kProcKoid;
    remote_api->OnAttach(transaction_id++, attach_request);

    // There should be a process.
    ASSERT_EQ(harness.debug_agent()->procs_.size(), 1u);
    // Should not come from limbo.
    EXPECT_FALSE(harness.debug_agent()->procs_.begin()->second->from_limbo());
  }

  // Killing now should work.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = kProcKoid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);

    // There should be no more processes.
    ASSERT_EQ(harness.debug_agent()->procs_.size(), 0u);

    // Killing again should fail.
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  // Add the process to the limbo.
  constexpr zx_koid_t kLimboProcKoid = 100;
  constexpr zx_koid_t kLimboThreadKoid = 101;
  MockProcessHandle mock_process(kLimboProcKoid, "proc");
  // This is a limbo process so we can not kill it.
  mock_process.set_kill_status(ZX_ERR_ACCESS_DENIED);
  MockThreadHandle mock_thread(kLimboThreadKoid, "thread");
  MockExceptionHandle mock_exception(kLimboThreadKoid);
  mock_process.set_threads({mock_thread});
  harness.system_interface()->mock_limbo_provider().AppendException(mock_process, mock_thread,
                                                                    mock_exception);

  // There should be no more processes.
  ASSERT_EQ(harness.debug_agent()->procs_.size(), 0u);

  // Killing now should release it.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = kLimboProcKoid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_OK);

    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 1u);
    EXPECT_EQ(harness.system_interface()->mock_limbo_provider().release_calls()[0], kLimboProcKoid);

    // Killing again should not find it.
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  harness.system_interface()->mock_limbo_provider().AppendException(mock_process, mock_thread,
                                                                    mock_exception);

  debug_ipc::AttachRequest attach_request = {};
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = kLimboProcKoid;
  remote_api->OnAttach(transaction_id++, attach_request);

  // There should be a process.
  ASSERT_EQ(harness.debug_agent()->procs_.size(), 1u);

  {
    auto it = harness.debug_agent()->procs_.find(kLimboProcKoid);
    ASSERT_NE(it, harness.debug_agent()->procs_.end());
    EXPECT_TRUE(harness.debug_agent()->procs_.begin()->second->from_limbo());

    // Killing it should free the process.
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = kLimboProcKoid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_OK);

    ASSERT_EQ(harness.debug_agent()->procs_.size(), 0u);

    // There should be a limbo process to be killed.
    ASSERT_EQ(harness.debug_agent()->killed_limbo_procs_.size(), 1u);
    EXPECT_EQ(harness.debug_agent()->killed_limbo_procs_.count(kLimboProcKoid), 1u);

    // There should've have been more release calls (yet).
    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 1u);

    // When the process "re-enters" the limbo, it should be removed.
    harness.system_interface()->mock_limbo_provider().AppendException(mock_process, mock_thread,
                                                                      mock_exception);
    harness.system_interface()->mock_limbo_provider().CallOnEnterLimbo();

    // There should not be an additional proc in the agent.
    ASSERT_EQ(harness.debug_agent()->procs_.size(), 0u);

    // There should've been a release call.
    ASSERT_EQ(harness.system_interface()->mock_limbo_provider().release_calls().size(), 2u);
    EXPECT_EQ(harness.system_interface()->mock_limbo_provider().release_calls()[1], kLimboProcKoid);
  }
}

TEST_F(DebugAgentTests, OnUpdateGlobalSettings) {
  MockDebugAgentHarness harness;
  RemoteAPI* remote_api = harness.debug_agent();

  // The default strategy should be first chance for a type that has yet to be
  // updated.
  EXPECT_EQ(debug_ipc::ExceptionStrategy::kFirstChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kGeneral));
  EXPECT_EQ(debug_ipc::ExceptionStrategy::kFirstChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kPageFault));

  {
    const debug_ipc::UpdateGlobalSettingsRequest request = {
        .exception_strategies =
            {
                {
                    .type = debug_ipc::ExceptionType::kGeneral,
                    .value = debug_ipc::ExceptionStrategy::kSecondChance,
                },
                {
                    .type = debug_ipc::ExceptionType::kPageFault,
                    .value = debug_ipc::ExceptionStrategy::kSecondChance,
                },
            },
    };
    debug_ipc::UpdateGlobalSettingsReply reply;
    remote_api->OnUpdateGlobalSettings(request, &reply);
    EXPECT_EQ(ZX_OK, reply.status);
  }

  EXPECT_EQ(debug_ipc::ExceptionStrategy::kSecondChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kGeneral));
  EXPECT_EQ(debug_ipc::ExceptionStrategy::kSecondChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kPageFault));

  {
    const debug_ipc::UpdateGlobalSettingsRequest request = {
        .exception_strategies =
            {
                {
                    .type = debug_ipc::ExceptionType::kGeneral,
                    .value = debug_ipc::ExceptionStrategy::kFirstChance,
                },
            },
    };
    debug_ipc::UpdateGlobalSettingsReply reply;
    remote_api->OnUpdateGlobalSettings(request, &reply);
    EXPECT_EQ(ZX_OK, reply.status);
  }

  EXPECT_EQ(debug_ipc::ExceptionStrategy::kFirstChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kGeneral));
  EXPECT_EQ(debug_ipc::ExceptionStrategy::kSecondChance,
            harness.debug_agent()->GetExceptionStrategy(debug_ipc::ExceptionType::kPageFault));
}

}  // namespace debug_agent
