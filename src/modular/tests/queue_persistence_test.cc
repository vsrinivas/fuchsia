// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/message_queue/cpp/message_queue_client.h>
#include <lib/message_queue/cpp/message_sender_client.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>
#include <test/modular/queuepersistence/cpp/fidl.h>

#include "peridot/lib/testing/session_shell_impl.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"

namespace {

// Timeout for each call to RunLoopWithTimeoutOrUntil().
constexpr auto kTimeout = zx::sec(30);

const char kModuleName[] = "module-name";
const char kStoryName[] = "story-name";

class QueuePersistenceTest : public modular::testing::TestHarnessFixture {};

// TODO(MF-386): Factor our some redundant pieces of TestAgent into a fake
// agent.
// An agent that provides QueuePersistenceTestService. Saves the last
// received message from the message queue.
class TestAgent : public modular::testing::FakeComponent,
                  public fuchsia::modular::Agent,
                  test::modular::queuepersistence::QueuePersistenceTestService {
 public:
  std::string GetLastReceivedMessage() { return last_received_message_; }

  void Connect(std::string requestor_url,
               fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                   services_request) override {
    services_.AddBinding(std::move(services_request));
  }

  void RunTask(std::string task_id, RunTaskCallback callback) override {}

 private:
  // |test::modular::queuepersistence::QueuePersistenceTestService|
  void GetMessageQueueToken(GetMessageQueueTokenCallback callback) override {
    msg_queue_.GetToken([callback = std::move(callback)](
                            const fidl::StringPtr& token) { callback(token); });
  }

  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    component_context()->svc()->Connect(component_context_.NewRequest());
    component_context()->svc()->Connect(agent_context_.NewRequest());

    component_context()->outgoing()->AddPublicService<fuchsia::modular::Agent>(
        [this](fidl::InterfaceRequest<fuchsia::modular::Agent> request) {
          bindings_.AddBinding(this, std::move(request));
        });

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Test Queue",
                                           msg_queue_.NewRequest());
    msg_queue_.RegisterReceiver(
        [this](std::string message, fit::function<void()> ack) {
          ack();
          last_received_message_ = message;
        });

    services_.AddService<
        test::modular::queuepersistence::QueuePersistenceTestService>(
        [this](fidl::InterfaceRequest<
               test::modular::queuepersistence::QueuePersistenceTestService>
                   request) {
          services_bindings_.AddBinding(this, std::move(request));
        });
  }

  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::AgentContextPtr agent_context_;
  fidl::BindingSet<fuchsia::modular::Agent> bindings_;
  modular::MessageQueueClient msg_queue_;
  component::ServiceNamespace services_;
  fidl::BindingSet<test::modular::queuepersistence::QueuePersistenceTestService>
      services_bindings_;

  std::string last_received_message_;
};

// A module that can connect to a TestAgent to send messages.
class TestModule : public modular::testing::FakeModule {
 public:
  TestModule()
      : FakeModule(/* on_intent_handled= */ [](fuchsia::modular::Intent) {}) {}

  test::modular::queuepersistence::QueuePersistenceTestService*
  agent_service() {
    return agent_service_.get();
  }

  void ConnectToAgent(std::string agent_url) {
    fuchsia::sys::ServiceProviderPtr agent_services;
    modular_component_context()->ConnectToAgent(
        agent_url, agent_services.NewRequest(), agent_controller_.NewRequest());
    component::ConnectToService(agent_services.get(),
                                agent_service_.NewRequest());
  }

  void DisconnectFromAgent() {
    agent_controller_.Unbind();
    agent_service_.Unbind();
  }

 private:
  fuchsia::modular::AgentControllerPtr agent_controller_;
  test::modular::queuepersistence::QueuePersistenceTestServicePtr
      agent_service_;
};

}  // namespace

// Sends a message to the message queue while the agent is offline, and expects
// that the agent receives the message when it starts again. This verifies that
// message queue messages are persisted even when there are no registered
// consumers.
TEST_F(QueuePersistenceTest, MessagePersistedToQueue) {
  modular::testing::TestHarnessBuilder builder;

  TestModule test_module;
  const auto test_module_url = builder.GenerateFakeUrl();
  builder.InterceptComponent(
      test_module.GetOnCreateHandler(),
      {.url = test_module_url,
       .sandbox_services = modular::testing::FakeModule::GetSandboxServices()});

  TestAgent test_agent;
  const auto test_agent_url = builder.GenerateFakeUrl();
  builder.InterceptComponent(
      test_agent.GetOnCreateHandler(),
      {.url = test_agent_url,
       .sandbox_services = {"fuchsia.modular.ComponentContext",
                            "fuchsia.modular.AgentContext"}});

  test_harness().events().OnNewComponent = builder.BuildOnNewComponentHandler();
  test_harness()->Run(builder.BuildSpec());

  // Add the test mod.
  fuchsia::modular::Intent intent;
  intent.handler = test_module_url;
  AddModToStory(std::move(intent), kModuleName, kStoryName);

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return test_module.is_running(); }, kTimeout));

  // Connect to the test agent from the test mod.
  test_module.ConnectToAgent(test_agent_url);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return test_agent.is_running(); },
                                        kTimeout));

  // Fetch the queue token from the agent's queue persistence service.
  std::string queue_token;
  test_module.agent_service()->GetMessageQueueToken(
      [&](const fidl::StringPtr& token) { queue_token = token; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return !queue_token.empty(); },
                                        kTimeout));

  // Disconnect from the agent. This should tear down the agent.
  test_module.DisconnectFromAgent();
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return !test_agent.is_running(); }, kTimeout));

  // Send a message to the stopped agent which should be persisted to local
  // storage. No triggers are set so the agent won't be automatically started.
  modular::MessageSenderClient message_sender;
  test_module.modular_component_context()->GetMessageSender(
      queue_token, message_sender.NewRequest());
  std::string kMessage = "message";
  message_sender.Send(kMessage);

  // The agent should receive the message upon restarting.
  test_module.ConnectToAgent(test_agent_url);
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return test_agent.GetLastReceivedMessage() == kMessage; },
      kTimeout));
}
