// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/agent_runner/agent_runner.h"

#include <memory>

#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/app/cpp/service_provider_impl.h>
#include <lib/app/cpp/testing/fake_launcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/fxl/macros.h>

#include "gtest/gtest.h"
#include "peridot/bin/user_runner/entity_provider_runner/entity_provider_runner.h"
#include "peridot/bin/user_runner/message_queue/message_queue_manager.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/fake_agent_runner_storage.h"
#include "peridot/lib/testing/mock_base.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace testing {
namespace {

using ::fuchsia::sys::testing::FakeLauncher;

class AgentRunnerTest : public TestWithLedger {
 public:
  AgentRunnerTest() = default;

  void SetUp() override {
    TestWithLedger::SetUp();

    mqm_.reset(new MessageQueueManager(
        ledger_client(), MakePageId("0123456789123456"), mq_data_dir_.path()));
    entity_provider_runner_.reset(new EntityProviderRunner(nullptr));
    // The |fuchsia::modular::UserIntelligenceProvider| below must be nullptr in
    // order for agent creation to be synchronous, which these tests assume.
    agent_runner_.reset(new AgentRunner(
        &launcher_, mqm_.get(), ledger_repository(), &agent_runner_storage_,
        token_provider_factory_.get(), nullptr, entity_provider_runner_.get()));
  }

  void TearDown() override {
    agent_runner_.reset();
    entity_provider_runner_.reset();
    mqm_.reset();

    TestWithLedger::TearDown();
  }

  MessageQueueManager* message_queue_manager() { return mqm_.get(); }

 protected:
  AgentRunner* agent_runner() { return agent_runner_.get(); }
  FakeLauncher* launcher() { return &launcher_; }

 private:
  FakeLauncher launcher_;

  files::ScopedTempDir mq_data_dir_;
  std::unique_ptr<MessageQueueManager> mqm_;
  FakeAgentRunnerStorage agent_runner_storage_;
  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;
  std::unique_ptr<AgentRunner> agent_runner_;

  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerTest);
};

class MyDummyAgent : fuchsia::modular::Agent,
                     public fuchsia::sys::ComponentController,
                     public testing::MockBase {
 public:
  MyDummyAgent(zx::channel directory_request,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl)
      : vfs_(async_get_default()),
        outgoing_directory_(fbl::AdoptRef(new fs::PseudoDir())),
        controller_(this, std::move(ctrl)),
        agent_binding_(this) {
    outgoing_directory_->AddEntry(
        fuchsia::modular::Agent::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          agent_binding_.Bind(std::move(channel));
          return ZX_OK;
        })));
    vfs_.ServeDirectory(outgoing_directory_, std::move(directory_request));
  }

  void KillApplication() { controller_.Unbind(); }

  size_t GetCallCount(const std::string func) { return counts.count(func); }

 private:
  // |ComponentController|
  void Kill() override { ++counts["Kill"]; }
  // |ComponentController|
  void Detach() override { ++counts["Detach"]; }
  // |ComponentController|
  void Wait(WaitCallback callback) override { ++counts["Wait"]; }

  // |fuchsia::modular::Agent|
  void Connect(
      fidl::StringPtr /*requestor_url*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    ++counts["Connect"];
  }

  // |fuchsia::modular::Agent|
  void RunTask(fidl::StringPtr /*task_id*/,
               RunTaskCallback /*callback*/) override {
    ++counts["RunTask"];
  }

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> outgoing_directory_;
  fidl::Binding<fuchsia::sys::ComponentController> controller_;
  fidl::Binding<fuchsia::modular::Agent> agent_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MyDummyAgent);
};

// Test that connecting to an agent will start it up.
// Then there should be an fuchsia::modular::Agent.Connect().
TEST_F(AgentRunnerTest, ConnectToAgent) {
  int agent_launch_count = 0;
  std::unique_ptr<MyDummyAgent> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&dummy_agent, &agent_launch_count](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        dummy_agent = std::make_unique<MyDummyAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
        ++agent_launch_count;
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopWithTimeoutOrUntil([&dummy_agent] {
    return dummy_agent && dummy_agent->GetCallCount("Connect") > 0;
  });
  EXPECT_EQ(1, agent_launch_count);
  dummy_agent->ExpectCalledOnce("Connect");
  dummy_agent->ExpectNoOtherCalls();

  // Connecting to the same agent again shouldn't launch a new instance and
  // shouldn't re-initialize the existing instance of the agent application,
  // but should call |Connect()|.

  fuchsia::modular::AgentControllerPtr agent_controller2;
  fuchsia::sys::ServiceProviderPtr incoming_services2;
  agent_runner()->ConnectToAgent("requestor_url2", kMyAgentUrl,
                                 incoming_services2.NewRequest(),
                                 agent_controller2.NewRequest());

  RunLoopWithTimeoutOrUntil([&dummy_agent] {
    return dummy_agent && dummy_agent->GetCallCount("Connect");
  });
  EXPECT_EQ(1, agent_launch_count);
  dummy_agent->ExpectCalledOnce("Connect");
  dummy_agent->ExpectNoOtherCalls();
}

// Test that if an agent application dies, it is removed from agent runner
// (which means outstanding AgentControllers are closed).
TEST_F(AgentRunnerTest, AgentController) {
  std::unique_ptr<MyDummyAgent> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterComponent(
      kMyAgentUrl,
      [&dummy_agent](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        dummy_agent = std::make_unique<MyDummyAgent>(
            std::move(launch_info.directory_request), std::move(ctrl));
      });

  fuchsia::sys::ServiceProviderPtr incoming_services;
  fuchsia::modular::AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopWithTimeoutOrUntil([&dummy_agent] { return !!dummy_agent; });
  dummy_agent->KillApplication();

  // fuchsia::modular::Agent application died, so check that
  // fuchsia::modular::AgentController dies here.
  agent_controller.set_error_handler(
      [&agent_controller] { agent_controller.Unbind(); });
  RunLoopWithTimeoutOrUntil(
      [&agent_controller] { return !agent_controller.is_bound(); });
  EXPECT_FALSE(agent_controller.is_bound());
}

}  // namespace
}  // namespace testing
}  // namespace modular
