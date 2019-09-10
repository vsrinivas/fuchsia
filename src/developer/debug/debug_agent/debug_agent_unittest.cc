// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/debug_agent.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/developer/debug/shared/message_loop_target.h"

#include "src/developer/debug/debug_agent/system_info.h"

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
  void HandleAttach(debug_ipc::AttachReply attach_reply) {
    attaches_.push_back(std::move(attach_reply));
  }

  const std::vector<debug_ipc::AttachReply>& attaches() const { return attaches_; }

 private:
  std::vector<debug_ipc::AttachReply> attaches_;
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

TEST(DebugAgent, OnAttach) {
  auto test_context = CreateTestContext();

  DebugAgent debug_agent(nullptr, test_context->object_provider);
  debug_agent.Connect(&test_context->stream_backend.stream());
  RemoteAPI* remote_api = &debug_agent;

  debug_ipc::AttachRequest attach_request;
  attach_request.type = debug_ipc::TaskType::kProcess;
  attach_request.koid = 11;

  remote_api->OnAttach(1u, attach_request);

  // We should've received a watch command (which does the low level exception watching).
  auto& watches = test_context->loop.watches();
  ASSERT_EQ(watches.size(), 1u);
  EXPECT_EQ(watches[0].process_name, "job1-p2");
  EXPECT_EQ(watches[0].process_handle, 11u);
  EXPECT_EQ(watches[0].process_koid, 11u);

  // We should've gotten an attach reply.
  auto& attaches = test_context->stream_backend.attaches();
  ASSERT_EQ(attaches.size(), 1u);
  EXPECT_EQ(attaches[0].koid, 11u);
  EXPECT_EQ(attaches[0].name, "job1-p2");
}

}  // namespace
}  // namespace debug_agent
