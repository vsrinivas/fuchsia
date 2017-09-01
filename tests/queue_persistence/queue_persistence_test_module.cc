// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/connect.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/tests/queue_persistence/queue_persistence_test_agent_interface.fidl.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 10000;
constexpr char kTestAgent[] =
    "file:///system/apps/modular_tests/queue_persistence_test_agent";

class ParentApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new ParentApp;  // deletes itself in Stop()
  }

 private:
  ParentApp() { TestInit(__FILE__); }
  ~ParentApp() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));

    initialized_.Pass();

    module_context_->GetComponentContext(component_context_.NewRequest());

    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(),
                     queue_persistence_agent_interface_.NewRequest());

    modular::testing::GetStore()->Get(
        "queue_persistence_test_agent_connected",
        [this](const fidl::String&) { AgentConnected(); });

    // Start a timer to call Story.Done() in case the test agent misbehaves and
    // we time out. If that happens, the module will exit normally through
    // Stop(), but the test will fail because some TestPoints will not have been
    // passed.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { module_context_->Done(); }),
        ftl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));
  }

  void AgentConnected() {
    agent_connected_.Pass();
    queue_persistence_agent_interface_->GetMessageQueueToken(
        [this](const fidl::String& token) { ReceivedQueueToken(token); });
  }

  void ReceivedQueueToken(const fidl::String& token) {
    queue_token_ = token;
    received_queue_persistence_token_.Pass();

    // Stop the agent.
    agent_controller_.reset();
    queue_persistence_agent_interface_.reset();
    modular::testing::GetStore()->Get(
        "queue_persistence_test_agent_stopped",
        [this](const fidl::String&) { AgentStopped(); });
  }

  void AgentStopped() {
    agent_stopped_.Pass();

    // Send a message to the stopped agent which should be persisted to local
    // storage. No triggers are set so the agent won't be automatically started.
    modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(queue_token_,
                                         message_sender.NewRequest());
    message_sender->Send("Queued message...");

    // Start the agent again.
    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(),
                     queue_persistence_agent_interface_.NewRequest());

    modular::testing::GetStore()->Get(
        "queue_persistence_test_agent_connected",
        [this](const fidl::String&) { AgentConnectedAgain(); });
  }

  void AgentConnectedAgain() {
    agent_connected_again_.Pass();
    modular::testing::GetStore()->Get(
        "queue_persistence_test_agent_received_message",
        [this](const fidl::String&) { AgentReceivedMessage(); });
  }

  void AgentReceivedMessage() {
    agent_received_message_.Pass();

    // Stop the agent again.
    agent_controller_.reset();
    queue_persistence_agent_interface_.reset();
    modular::testing::GetStore()->Get(
        "queue_persistence_test_agent_stopped",
        [this](const fidl::String&) { module_context_->Done(); });
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  modular::ModuleContextPtr module_context_;
  modular::AgentControllerPtr agent_controller_;
  modular::testing::QueuePersistenceAgentInterfacePtr
      queue_persistence_agent_interface_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  std::string queue_token_;

  using TestPoint = modular::testing::TestPoint;
  TestPoint initialized_{"Root module initialized"};
  TestPoint received_queue_persistence_token_{
      "Received queue_persistence token"};
  TestPoint stopped_{"Root module stopped"};
  TestPoint agent_connected_{"Agent accepted connection"};
  TestPoint agent_connected_again_{"Agent accepted connection, again"};
  TestPoint agent_received_message_{"Agent received message"};
  TestPoint agent_stopped_{"Agent stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
