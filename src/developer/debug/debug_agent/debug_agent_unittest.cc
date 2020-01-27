// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch_provider_impl.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/system_info.h"
#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace fuchsia::exception;

namespace debug_agent {
namespace {

// Setup -------------------------------------------------------------------------------------------

class DebugAgentMessageLoop : public debug_ipc::PlatformMessageLoop {
 public:
  DebugAgentMessageLoop() {
    std::string error_message;
    bool success = Init(&error_message);
    FXL_CHECK(success) << error_message;
  }
  ~DebugAgentMessageLoop() { Cleanup(); }

  void StopWatching(int id) override {}

  zx_status_t WatchProcessExceptions(WatchProcessConfig config, WatchHandle* out) override {
    watches_.push_back(std::move(config));
    *out = WatchHandle(this, next_watch_id_++);
    return ZX_OK;
  }

  const std::vector<WatchProcessConfig> watches() const { return watches_; }

 private:
  int next_watch_id_ = 1;

  std::vector<WatchProcessConfig> watches_;
};

class DebugAgentStreamBackend : public LocalStreamBackend {
 public:
  void HandleAttach(debug_ipc::AttachReply attach_reply) override {
    attach_replies_.push_back(std::move(attach_reply));
  }

  void HandleNotifyProcessStarting(debug_ipc::NotifyProcessStarting notification) override {
    process_starts_.push_back(std::move(notification));
  }

  void HandleNotifyModules(debug_ipc::NotifyModules modules) override {
    modules_.push_back(std::move(modules));
  }

  const std::vector<debug_ipc::AttachReply>& attach_replies() const { return attach_replies_; }
  const std::vector<debug_ipc::NotifyProcessStarting>& process_starts() const {
    return process_starts_;
  }
  const std::vector<debug_ipc::NotifyModules> modules() const { return modules_; }

 private:
  std::vector<debug_ipc::AttachReply> attach_replies_;
  std::vector<debug_ipc::NotifyProcessStarting> process_starts_;
  std::vector<debug_ipc::NotifyModules> modules_;
};

class DebugAgentMockProcess : public MockProcess {
 public:
  DebugAgentMockProcess(DebugAgent* debug_agent, zx_koid_t koid, std::string name,
                        std::shared_ptr<ObjectProvider> object_provider,
                        std::shared_ptr<arch::ArchProvider> arch_provider)
      : MockProcess(debug_agent, koid, std::move(name), std::move(arch_provider),
                    std::move(object_provider)) {}

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

class MockLimboProvider : public LimboProvider {
 public:
  MockLimboProvider() : LimboProvider(nullptr) {}

  const std::map<zx_koid_t, ProcessExceptionMetadata>& Limbo() const override { return limbo_; }
  const std::vector<zx_koid_t> release_calls() const { return release_calls_; }

  bool Valid() const override { return true; }

  zx_status_t RetrieveException(zx_koid_t process_koid,
                                fuchsia::exception::ProcessException* out) override {
    auto it = limbo_.find(process_koid);
    if (it == limbo_.end())
      return ZX_ERR_NOT_FOUND;

    *out = ToProcessException(it->second);

    limbo_.erase(it);
    return ZX_OK;
  }

  void AppendException(const MockProcessObject* process, const MockThreadObject* thread,
                       ExceptionType exception_type) {
    ExceptionInfo info = {};
    info.process_koid = process->koid;
    info.thread_koid = thread->koid;
    info.type = exception_type;

    ProcessExceptionMetadata metadata = {};
    metadata.set_info(std::move(info));
    metadata.set_process(process->GetHandle());
    metadata.set_thread(thread->GetHandle());

    limbo_[process->koid] = std::move(metadata);
  }

  void CallOnEnterLimbo() {
    FXL_DCHECK(on_enter_limbo_);

    std::vector<ProcessExceptionMetadata> processes;
    processes.reserve(limbo_.size());
    for (const auto& [process_koid, metadata] : limbo_) {
      processes.push_back(CopyMetadata(metadata));
    }

    on_enter_limbo_(std::move(processes));
  }

  zx_status_t ReleaseProcess(zx_koid_t process_koid) override {
    release_calls_.push_back(process_koid);

    auto it = limbo_.find(process_koid);
    if (it == limbo_.end())
      return ZX_ERR_NOT_FOUND;

    limbo_.erase(it);
    return ZX_OK;
  }

 private:
  ProcessExceptionMetadata CopyMetadata(const ProcessExceptionMetadata& metadata) {
    ProcessExceptionMetadata exception = {};

    exception.set_info(metadata.info());
    exception.set_process(zx::process(metadata.info().process_koid));
    exception.set_thread(zx::thread(metadata.info().thread_koid));

    return exception;
  }

  ProcessException ToProcessException(const ProcessExceptionMetadata& metadata) {
    ProcessException exception = {};

    exception.set_info(metadata.info());
    exception.set_exception(zx::exception(metadata.info().thread_koid));
    exception.set_process(zx::process(metadata.info().process_koid));
    exception.set_thread(zx::thread(metadata.info().thread_koid));

    return exception;
  }

  std::map<zx_koid_t, ProcessExceptionMetadata> limbo_;
  std::vector<zx_koid_t> release_calls_;
};

class TestObjectProvider : public MockObjectProvider {
 public:
  zx_status_t Kill(zx_handle_t) override { return next_kill_status_; }

  void set_next_kill_status(zx_status_t status) { next_kill_status_ = status; }

 private:
  zx_status_t next_kill_status_ = ZX_OK;
};

std::pair<const MockProcessObject*, const MockThreadObject*> GetProcessThread(
    const MockObjectProvider& object_provider, const std::string& process_name,
    const std::string& thread_name) {
  const MockProcessObject* process = object_provider.ProcessByName(process_name);
  FXL_DCHECK(process);
  const MockThreadObject* thread = process->GetThread(thread_name);
  FXL_DCHECK(thread);

  return {process, thread};
}

struct TestContext {
  DebugAgentMessageLoop loop;
  DebugAgentStreamBackend stream_backend;

  std::shared_ptr<TestObjectProvider> object_provider;
  std::shared_ptr<MockLimboProvider> limbo_provider;
  std::shared_ptr<arch::ArchProvider> arch_provider;
};

SystemProviders ToSystemProviders(const TestContext& context) {
  SystemProviders providers;
  providers.arch_provider = context.arch_provider;
  providers.limbo_provider = context.limbo_provider;
  providers.object_provider = context.object_provider;

  return providers;
}

std::unique_ptr<TestContext> CreateTestContext() {
  auto context = std::make_unique<TestContext>();
  context->arch_provider = std::make_shared<ArchProviderImpl>();
  context->limbo_provider = std::make_shared<MockLimboProvider>();
  context->object_provider = std::make_unique<TestObjectProvider>();
  FillInMockObjectProvider(context->object_provider.get());
  return context;
}

}  // namespace

// Tests -------------------------------------------------------------------------------------------

class DebugAgentTests : public ::testing::Test {};

TEST_F(DebugAgentTests, OnGlobalStatus) {
  auto test_context = CreateTestContext();

  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  debug_ipc::StatusRequest request = {};

  debug_ipc::StatusReply reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.processes.size(), 0u);

  constexpr uint64_t kProcessKoid1 = 0x1234;
  const std::string kProcessName1 = "process-1";
  constexpr uint64_t kProcess1ThreadKoid1 = 0x1;

  auto process1 =
      std::make_unique<MockProcess>(nullptr, kProcessKoid1, kProcessName1,
                                    test_context->arch_provider, test_context->object_provider);
  process1->AddThread(kProcess1ThreadKoid1);
  debug_agent.InjectProcessForTest(std::move(process1));

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

  auto process2 =
      std::make_unique<MockProcess>(nullptr, kProcessKoid2, kProcessName2,
                                    test_context->arch_provider, test_context->object_provider);
  process2->AddThread(kProcess2ThreadKoid1);
  process2->AddThread(kProcess2ThreadKoid2);
  debug_agent.InjectProcessForTest(std::move(process2));

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
  const MockObjectProvider& object_provider = *test_context->object_provider;

  const std::string kLimboProcess1 = "job1-p1";
  const std::string kLimboProcess1Thread = "initial-thread";
  constexpr ExceptionType kLimboException1 = ExceptionType::FATAL_PAGE_FAULT;
  auto [limbo_proc1, limbo_thread1] =
      GetProcessThread(object_provider, kLimboProcess1, kLimboProcess1Thread);

  const std::string kLimboProcess2 = "job121-p2";
  const std::string kLimboProcess2Thread = "second-thread";
  constexpr ExceptionType kLimboException2 = ExceptionType::UNALIGNED_ACCESS;
  auto [limbo_proc2, limbo_thread2] =
      GetProcessThread(object_provider, kLimboProcess2, kLimboProcess2Thread);

  test_context->limbo_provider->AppendException(limbo_proc1, limbo_thread1, kLimboException1);
  test_context->limbo_provider->AppendException(limbo_proc2, limbo_thread2, kLimboException2);

  reply = {};
  remote_api->OnStatus(request, &reply);

  // The attached processes should still be there.
  ASSERT_EQ(reply.processes.size(), 2u);

  // The limbo processes should be there.
  ASSERT_EQ(reply.limbo.size(), 2u);
  EXPECT_EQ(reply.limbo[0].process_koid, limbo_proc1->koid);
  EXPECT_EQ(reply.limbo[0].process_name, limbo_proc1->name);
  ASSERT_EQ(reply.limbo[0].threads.size(), 1u);
  EXPECT_EQ(reply.limbo[0].threads[0].process_koid, limbo_proc1->koid);
  EXPECT_EQ(reply.limbo[0].threads[0].thread_koid, limbo_thread1->koid);
  EXPECT_EQ(reply.limbo[0].threads[0].name, limbo_thread1->name);
  EXPECT_EQ(reply.limbo[0].threads[0].state, debug_ipc::ThreadRecord::State::kBlocked);
  EXPECT_EQ(reply.limbo[0].threads[0].blocked_reason,
            debug_ipc::ThreadRecord::BlockedReason::kException);

  // TODO(donosoc): Add exception type.
}

TEST_F(DebugAgentTests, OnProcessStatus) {
  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  constexpr uint64_t kProcessKoid1 = 0x1234;
  std::string kProcessName1 = "process-1";
  auto process1 = std::make_unique<DebugAgentMockProcess>(
      &debug_agent, kProcessKoid1, kProcessName1, test_context->object_provider,
      test_context->arch_provider);
  debug_agent.InjectProcessForTest(std::move(process1));

  constexpr uint64_t kProcessKoid2 = 0x5678;
  std::string kProcessName2 = "process-2";
  auto process2 = std::make_unique<DebugAgentMockProcess>(
      &debug_agent, kProcessKoid2, kProcessName2, test_context->object_provider,
      test_context->arch_provider);
  auto* process2_ptr = process2.get();
  debug_agent.InjectProcessForTest(std::move(process2));

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

  test_context->loop.RunUntilNoTasks();

  auto& process_starts = test_context->stream_backend.process_starts();
  ASSERT_EQ(process_starts.size(), 1u);
  EXPECT_EQ(process_starts[0].koid, kProcessKoid2);
  EXPECT_EQ(process_starts[0].name, kProcessName2);

  auto& modules = test_context->stream_backend.modules();
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

  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  debug_ipc::AttachRequest attach_request;
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = -1;

  remote_api->OnAttach(transaction_id++, attach_request);

  {
    // Should've gotten an attach reply.
    auto& attach_replies = test_context->stream_backend.attach_replies();
    ASSERT_EQ(attach_replies.size(), 1u);
    EXPECT_ZX_EQ(attach_replies[0].status, ZX_ERR_NOT_FOUND);

    // There should be no attach watch.
    auto& watches = test_context->loop.watches();
    ASSERT_EQ(watches.size(), 0u);
  }

  auto [proc_object, thread_object] =
      GetProcessThread(*test_context->object_provider, "job11-p1", "second-thread");
  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);

  // Even with limbo it should fail.
  remote_api->OnAttach(transaction_id++, attach_request);

  {
    // Should've gotten an attach reply.
    auto& attach_replies = test_context->stream_backend.attach_replies();
    ASSERT_EQ(attach_replies.size(), 2u);
    EXPECT_ZX_EQ(attach_replies[1].status, ZX_ERR_NOT_FOUND);

    // There should be no attach watch.
    auto& watches = test_context->loop.watches();
    ASSERT_EQ(watches.size(), 0u);
  }
}

TEST_F(DebugAgentTests, OnAttach) {
  uint32_t transaction_id = 1u;

  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  debug_ipc::AttachRequest attach_request;
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = 11;

  remote_api->OnAttach(transaction_id++, attach_request);

  // We should've received a watch command (which does the low level exception watching).
  auto& watches = test_context->loop.watches();
  ASSERT_EQ(watches.size(), 1u);
  EXPECT_EQ(watches[0].process_name, "job1-p2");
  EXPECT_EQ(watches[0].process_handle, 11u);
  EXPECT_EQ(watches[0].process_koid, 11u);

  // We should've gotten an attach reply.
  auto& attach_replies = test_context->stream_backend.attach_replies();
  auto reply = attach_replies.back();
  ASSERT_EQ(attach_replies.size(), 1u);
  EXPECT_ZX_EQ(reply.status, ZX_OK);
  EXPECT_EQ(reply.koid, 11u);
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
  uint32_t transaction_id = 1u;

  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  auto [proc_object, thread_object] =
      GetProcessThread(*test_context->object_provider, "job11-p1", "second-thread");
  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);

  debug_ipc::AttachRequest attach_request = {};
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = proc_object->koid;
  remote_api->OnAttach(transaction_id++, attach_request);

  // We should've received a watch command (which does the low level exception watching).
  auto& watches = test_context->loop.watches();
  ASSERT_EQ(watches.size(), 1u);
  EXPECT_EQ(watches[0].process_name, proc_object->name);
  EXPECT_EQ(watches[0].process_handle, proc_object->koid);
  EXPECT_EQ(watches[0].process_koid, proc_object->koid);

  // We should've gotten an attach reply.
  auto& attach_replies = test_context->stream_backend.attach_replies();
  auto reply = attach_replies.back();
  ASSERT_EQ(attach_replies.size(), 1u);
  EXPECT_ZX_EQ(reply.status, ZX_OK);
  EXPECT_EQ(reply.koid, proc_object->koid);
  EXPECT_EQ(reply.name, proc_object->name);

  {
    DebuggedProcess* process = debug_agent.GetDebuggedProcess(proc_object->koid);
    ASSERT_TRUE(process);
    auto threads = process->GetThreads();
    ASSERT_EQ(threads.size(), 2u);

    // Search for the exception thread.
    DebuggedThread* exception_thread = nullptr;
    for (DebuggedThread* thread : threads) {
      if (thread->koid() == thread_object->koid) {
        exception_thread = thread;
        break;
      }
    }

    ASSERT_TRUE(exception_thread);
    ASSERT_TRUE(exception_thread->IsInException());
    EXPECT_EQ(exception_thread->exception_handle().get(), thread_object->koid);
  }
}

TEST_F(DebugAgentTests, OnEnterLimbo) {
  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());

  auto [proc_object, thread_object] =
      GetProcessThread(*test_context->object_provider, "job11-p1", "second-thread");
  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);

  // Call the limbo.
  test_context->limbo_provider->CallOnEnterLimbo();

  // Should've sent a notification.
  {
    ASSERT_EQ(test_context->stream_backend.process_starts().size(), 1u);

    auto& process_start = test_context->stream_backend.process_starts()[0];
    EXPECT_EQ(process_start.type, debug_ipc::NotifyProcessStarting::Type::kLimbo);
    EXPECT_EQ(process_start.koid, proc_object->koid);
    EXPECT_EQ(process_start.component_id, 0u);
    EXPECT_EQ(process_start.name, proc_object->name);
  }
}

TEST_F(DebugAgentTests, DetachFromLimbo) {
  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  auto [proc_object, thread_object] =
      GetProcessThread(*test_context->object_provider, "job11-p1", "second-thread");

  // Attempting to detach to a process that doesn't exist should fail.
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = proc_object->koid;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_ERR_NOT_FOUND);
    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 0u);
  }

  // Adding it should now find it and remove it.
  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = proc_object->koid;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_OK);
    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 1u);
    EXPECT_EQ(test_context->limbo_provider->release_calls()[0], proc_object->koid);
  }

  // This should've remove it from limbo, trying it again should fail.
  {
    debug_ipc::DetachRequest request = {};
    request.type = debug_ipc::TaskType::kProcess;
    request.koid = proc_object->koid;

    debug_ipc::DetachReply reply = {};
    remote_api->OnDetach(request, &reply);

    ASSERT_ZX_EQ(reply.status, ZX_ERR_NOT_FOUND);
    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 1u);
    EXPECT_EQ(test_context->limbo_provider->release_calls()[0], proc_object->koid);
  }
}

TEST_F(DebugAgentTests, Kill) {
  uint32_t transaction_id = 1u;

  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, ToSystemProviders(*test_context));
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  auto [proc_object, thread_object] =
      GetProcessThread(*test_context->object_provider, "job11-p1", "second-thread");

  // Attempt to kill a process that's not there should fail.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = proc_object->koid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  // Attach to a process so that the debugger knows about it.
  {
    debug_ipc::AttachRequest attach_request = {};
    attach_request.type = debug_ipc::TaskType::kProcess;
    attach_request.koid = proc_object->koid;
    remote_api->OnAttach(transaction_id++, attach_request);

    // There should be a process.
    ASSERT_EQ(debug_agent.procs_.size(), 1u);
    // Should not come from limbo.
    EXPECT_FALSE(debug_agent.procs_.begin()->second->from_limbo());
  }

  // Killing now should work.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = proc_object->koid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);

    // There should be no more processes.
    ASSERT_EQ(debug_agent.procs_.size(), 0u);

    // Killing again should fail.
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  // We now add the process to the limbo.
  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);

  // There should be no more processes.
  ASSERT_EQ(debug_agent.procs_.size(), 0u);

  // Killing now should release it.
  {
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = proc_object->koid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_OK);

    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 1u);
    EXPECT_EQ(test_context->limbo_provider->release_calls()[0], proc_object->koid);

    // Killing again should not find it.
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_ERR_NOT_FOUND);
  }

  test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                ExceptionType::FATAL_PAGE_FAULT);

  // This is a limbo process, so we cannot kill it.
  test_context->object_provider->set_next_kill_status(ZX_ERR_ACCESS_DENIED);

  debug_ipc::AttachRequest attach_request = {};
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = proc_object->koid;
  remote_api->OnAttach(transaction_id++, attach_request);

  // There should be a process.
  ASSERT_EQ(debug_agent.procs_.size(), 1u);

  {
    auto it = debug_agent.procs_.find(proc_object->koid);
    ASSERT_NE(it, debug_agent.procs_.end());
    EXPECT_TRUE(debug_agent.procs_.begin()->second->from_limbo());

    // Killing it should free the process.
    debug_ipc::KillRequest kill_request = {};
    kill_request.process_koid = proc_object->koid;

    debug_ipc::KillReply kill_reply = {};
    remote_api->OnKill(kill_request, &kill_reply);
    ASSERT_ZX_EQ(kill_reply.status, ZX_OK);

    ASSERT_EQ(debug_agent.procs_.size(), 0u);

    // There should be a limbo process to be killed.
    ASSERT_EQ(debug_agent.killed_limbo_procs_.size(), 1u);
    EXPECT_EQ(debug_agent.killed_limbo_procs_.count(proc_object->koid), 1u);

    // There should've have been more release calls (yet).
    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 1u);

    // When the process "re-enters" the limbo, it should be removed.
    test_context->limbo_provider->AppendException(proc_object, thread_object,
                                                  ExceptionType::FATAL_PAGE_FAULT);
    test_context->limbo_provider->CallOnEnterLimbo();

    // There should not be an additional proc in the agent.
    ASSERT_EQ(debug_agent.procs_.size(), 0u);

    // There should've been a release call.
    ASSERT_EQ(test_context->limbo_provider->release_calls().size(), 2u);
    EXPECT_EQ(test_context->limbo_provider->release_calls()[1], proc_object->koid);
  }
}

}  // namespace debug_agent
