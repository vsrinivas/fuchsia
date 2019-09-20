// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/system_info.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace {

class DebugAgentMessageLoop : public debug_ipc::MessageLoopTarget {
 public:
  DebugAgentMessageLoop() { Init(); }
  ~DebugAgentMessageLoop() { Cleanup(); }

  void Init() override { MessageLoopTarget::Init(); }
  void Cleanup() override { MessageLoopTarget::Cleanup(); }
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
                        std::shared_ptr<ObjectProvider> object_provider)
      : MockProcess(koid, std::move(name), std::move(object_provider)), debug_agent_(debug_agent) {}

  ~DebugAgentMockProcess() = default;

  void SuspendAndSendModulesIfKnown() override {
    // Send the modules over to the ipc.
    debug_ipc::MessageWriter writer;
    debug_ipc::WriteNotifyModules(modules_to_send_, &writer);
    debug_agent_->stream()->Write(writer.MessageComplete());
  };

  void set_modules_to_send(debug_ipc::NotifyModules m) { modules_to_send_ = std::move(m); }

 private:
  debug_ipc::NotifyModules modules_to_send_;

  DebugAgent* debug_agent_ = nullptr;
};

struct TestContext {
  DebugAgentMessageLoop loop;
  std::shared_ptr<ObjectProvider> object_provider;
  DebugAgentStreamBackend stream_backend;
};

std::unique_ptr<TestContext> CreateTestContext() {
  auto context = std::make_unique<TestContext>();
  context->object_provider = CreateDefaultMockObjectProvider();
  return context;
}

TEST(DebugAgent, OnGlobalStatus) {
  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, test_context->object_provider);
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  debug_ipc::StatusRequest request = {};

  debug_ipc::StatusReply reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.process_koids.size(), 0u);

  constexpr uint64_t kProcessKoid1 = 0x1234;
  auto process1 = std::make_unique<MockProcess>(kProcessKoid1, nullptr);
  debug_agent.InjectProcessForTest(std::move(process1));

  reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.process_koids.size(), 1u);
  EXPECT_EQ(reply.process_koids[0], kProcessKoid1);

  constexpr uint64_t kProcessKoid2 = 0x5678;
  auto process2 = std::make_unique<MockProcess>(kProcessKoid2, nullptr);
  debug_agent.InjectProcessForTest(std::move(process2));

  reply = {};
  remote_api->OnStatus(request, &reply);

  ASSERT_EQ(reply.process_koids.size(), 2u);
  EXPECT_EQ(reply.process_koids[0], kProcessKoid1);
  EXPECT_EQ(reply.process_koids[1], kProcessKoid2);
}

TEST(DebugAgent, OnProcessStatus) {
  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, test_context->object_provider);
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  constexpr uint64_t kProcessKoid1 = 0x1234;
  std::string kProcessName1 = "process-1";
  auto process1 =
      std::make_unique<DebugAgentMockProcess>(&debug_agent, kProcessKoid1, kProcessName1, nullptr);
  debug_agent.InjectProcessForTest(std::move(process1));

  constexpr uint64_t kProcessKoid2 = 0x5678;
  std::string kProcessName2 = "process-2";
  auto process2 =
      std::make_unique<DebugAgentMockProcess>(&debug_agent, kProcessKoid2, kProcessName2, nullptr);
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
  modules_to_send.modules.push_back({"module-1", 0x1, "build-1"});
  modules_to_send.modules.push_back({"module-2", 0x2, "build-2"});
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
  ASSERT_EQ(modules[0].modules[0].build_id, modules_to_send.modules[0].build_id);
  ASSERT_EQ(modules[0].modules[1].name, modules_to_send.modules[1].name);
  ASSERT_EQ(modules[0].modules[1].base, modules_to_send.modules[1].base);
  ASSERT_EQ(modules[0].modules[1].build_id, modules_to_send.modules[1].build_id);
}

TEST(DebugAgent, OnAttach) {
  uint32_t transaction_id = 1u;

  auto test_context = CreateTestContext();
  DebugAgent debug_agent(nullptr, test_context->object_provider);
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
  EXPECT_EQ(reply.status, ZX_OK) << zx_status_get_string(reply.status);
  EXPECT_EQ(reply.koid, 11u);
  EXPECT_EQ(reply.name, "job1-p2");

  // Asking for some invalid process should fail.
  attach_request.koid = 0x231315;  // Some invalid value.
  remote_api->OnAttach(transaction_id++, attach_request);

  // We should've gotten an error reply.
  ASSERT_EQ(attach_replies.size(), 2u);
  reply = attach_replies.back();
  EXPECT_EQ(reply.status, ZX_ERR_NOT_FOUND) << zx_status_get_string(reply.status);

  // Attaching to a third process should work.
  attach_request.koid = 21u;
  remote_api->OnAttach(transaction_id++, attach_request);

  ASSERT_EQ(attach_replies.size(), 3u);
  reply = attach_replies.back();
  EXPECT_EQ(reply.status, ZX_OK) << zx_status_get_string(reply.status);
  EXPECT_EQ(reply.koid, 21u);
  EXPECT_EQ(reply.name, "job121-p2");

  // Attaching again to a process should fail.
  remote_api->OnAttach(transaction_id++, attach_request);

  ASSERT_EQ(attach_replies.size(), 4u);
  reply = attach_replies.back();
  EXPECT_EQ(reply.status, ZX_ERR_ALREADY_BOUND) << zx_status_get_string(reply.status);
}

}  // namespace
}  // namespace debug_agent
