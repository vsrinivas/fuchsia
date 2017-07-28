// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_runner.h"
#include "application/lib/app/service_provider_impl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/testing/fake_application_launcher.h"
#include "apps/modular/lib/testing/ledger_repository_for_testing.h"
#include "apps/modular/lib/testing/test_with_message_loop.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace testing {
namespace {

class FakeAgentRunnerStorage : public AgentRunnerStorage {
 public:
  FakeAgentRunnerStorage() = default;

  // |AgentRunnerStorage|
  void Initialize(NotificationDelegate* delegate,
                  const ftl::Closure done) override {
    done();
  }

  // |AgentRunnerStorage|
  void WriteTask(const std::string& agent_url, TriggerInfo info,
                 const std::function<void(bool)> done) override {
    done(true);
  }

  // |AgentRunnerStorage|
  void DeleteTask(const std::string& agent_url, const std::string& task_id,
                  const std::function<void(bool)> done) override {
    done(true);
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeAgentRunnerStorage);
};

class AgentRunnerTest : public TestWithMessageLoop {
 public:
  AgentRunnerTest()
      : ledger_repo_for_testing_(
            LedgerRepositoryForTesting::GetSingleton("/tmp/ledger_test_repo")) {
  }

  void SetUp() override {
    ledger_repo_for_testing_->ledger_repository()->GetLedger(
        to_array("agent_ledger_test"), agent_ledger_.NewRequest(),
        [](ledger::Status status) { ASSERT_EQ(ledger::Status::OK, status); });

    agent_ledger_->GetPage(to_array("0123456789123456"),
                           message_queue_page_.NewRequest(),
                           [](ledger::Status status) {
                             ASSERT_TRUE(status == ledger::Status::OK);
                           });

    mqm_.reset(new MessageQueueManager(std::move(message_queue_page_),
                                       "/tmp/test_mq_data"));

    agent_runner_.reset(new AgentRunner(
        &launcher_, mqm_.get(), ledger_repo_for_testing_->ledger_repository(),
        &agent_runner_storage_, token_provider_factory_.get(),
        ui_provider_.get()));
  }

  void TearDown() override {
    bool repo_deleted = false;
    ledger_repo_for_testing_->Reset([&repo_deleted] { repo_deleted = true; });
    if (!repo_deleted) {
      RunLoopUntil([&repo_deleted] { return repo_deleted; });
    }
  }

  MessageQueueManager* message_queue_manager() { return mqm_.get(); }

 protected:
  AgentRunner* agent_runner() { return agent_runner_.get(); }
  FakeApplicationLauncher* launcher() { return &launcher_; }

 private:
  FakeApplicationLauncher launcher_;
  LedgerRepositoryForTesting* ledger_repo_for_testing_;

  ledger::LedgerPtr agent_ledger_;
  ledger::PagePtr agent_ledger_page_;
  ledger::PagePtr message_queue_page_;
  std::unique_ptr<MessageQueueManager> mqm_;
  FakeAgentRunnerStorage agent_runner_storage_;
  std::unique_ptr<AgentRunner> agent_runner_;

  auth::TokenProviderFactoryPtr token_provider_factory_;
  maxwell::UserIntelligenceProviderPtr ui_provider_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AgentRunnerTest);
};

class MyDummyAgent : public Agent, public app::ApplicationController {
 public:
  MyDummyAgent(fidl::InterfaceRequest<app::ServiceProvider> outgoing_services,
               fidl::InterfaceRequest<app::ApplicationController> ctrl)
      : app_controller_(this, std::move(ctrl)), agent_binding_(this) {
    outgoing_services_.AddService<Agent>(
        [this](fidl::InterfaceRequest<Agent> request) {
          agent_binding_.Bind(std::move(request));
        });
    outgoing_services_.AddBinding(std::move(outgoing_services));
  }

  void KillApplication() { app_controller_.Close(); }

 private:
  // |ApplicationController|
  void Kill() override {}
  // |ApplicationController|
  void Detach() override {}

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_initialized = true;
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    agent_connected = true;
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const RunTaskCallback& callback) override {}

  // |Agent|
  void Stop(const StopCallback& callback) override {
    agent_stopped = true;
    callback();
  }

 public:
  bool agent_initialized = false;
  bool agent_connected = false;
  bool agent_stopped = false;

 private:
  app::ServiceProviderImpl outgoing_services_;
  fidl::Binding<app::ApplicationController> app_controller_;
  fidl::Binding<modular::Agent> agent_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MyDummyAgent);
};

// Test that connecting to an agent will start it up and calls
// Agent.Initialize(); once Initialize() responds, there should be an
// Agent.Connect().
TEST_F(AgentRunnerTest, ConnectToAgent) {
  bool agent_launched = false;

  std::unique_ptr<MyDummyAgent> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterApplication(
      kMyAgentUrl,
      [&dummy_agent, &agent_launched](
          app::ApplicationLaunchInfoPtr launch_info,
          fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        dummy_agent.reset(new MyDummyAgent(std::move(launch_info->services),
                                           std::move(ctrl)));
        agent_launched = true;
      });

  app::ServiceProviderPtr incoming_services;
  AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  RunLoopUntil([&agent_launched] { return agent_launched; });
  EXPECT_TRUE(agent_launched);

  RunLoopUntil([&dummy_agent] { return dummy_agent->agent_initialized; });
  EXPECT_TRUE(dummy_agent->agent_initialized);

  RunLoopUntil([&dummy_agent] { return dummy_agent->agent_connected; });
  EXPECT_TRUE(dummy_agent->agent_connected);

  // Connecting to the same agent again shouldn't initialize or launch a new
  // instance of the agent application.
  agent_launched = false;
  dummy_agent->agent_initialized = false;
  dummy_agent->agent_connected = true;

  AgentControllerPtr agent_controller2;
  app::ServiceProviderPtr incoming_services2;
  agent_runner()->ConnectToAgent("requestor_url2", kMyAgentUrl,
                                 incoming_services2.NewRequest(),
                                 agent_controller2.NewRequest());

  RunLoopUntil([&dummy_agent] { return dummy_agent->agent_connected; });
  EXPECT_FALSE(agent_launched);
  EXPECT_FALSE(dummy_agent->agent_initialized);
  EXPECT_TRUE(dummy_agent->agent_connected);
}

// Test that if an agent application dies, it is removed from agent runner
// (which means outstanding AgentControllers are closed).
TEST_F(AgentRunnerTest, AgentController) {
  std::unique_ptr<MyDummyAgent> dummy_agent;
  constexpr char kMyAgentUrl[] = "file:///my_agent";
  launcher()->RegisterApplication(
      kMyAgentUrl,
      [&dummy_agent](app::ApplicationLaunchInfoPtr launch_info,
                     fidl::InterfaceRequest<app::ApplicationController> ctrl) {
        dummy_agent.reset(new MyDummyAgent(std::move(launch_info->services),
                                           std::move(ctrl)));
      });

  app::ServiceProviderPtr incoming_services;
  AgentControllerPtr agent_controller;
  agent_runner()->ConnectToAgent("requestor_url", kMyAgentUrl,
                                 incoming_services.NewRequest(),
                                 agent_controller.NewRequest());

  dummy_agent->KillApplication();

  // Agent application died, so check that AgentController dies here.
  agent_controller.set_connection_error_handler(
      [&agent_controller] { agent_controller.reset(); });
  RunLoopUntil([&agent_controller] { return !agent_controller.is_bound(); });
  EXPECT_FALSE(agent_controller.is_bound());
}

}  // namespace
}  // namespace testing
}  // namespace modular
