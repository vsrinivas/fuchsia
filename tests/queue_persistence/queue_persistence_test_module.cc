// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/module/fidl/module.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/lib/util/weak_callback.h"
#include "peridot/tests/queue_persistence/queue_persistence_test_agent_interface.fidl.h"

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 10000;
constexpr char kTestAgent[] =
    "file:///system/test/modular_tests/queue_persistence_test_agent";

class ParentApp {
 public:
  ParentApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host), weak_ptr_factory_(this) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();

    module_host_->module_context()->GetComponentContext(
        component_context_.NewRequest());

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
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        modular::WeakCallback(
            weak_ptr_factory_.GetWeakPtr(),
            [this] { module_host_->module_context()->Done(); }),
        fxl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));
  }

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
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
    modular::testing::GetStore()->Get("queue_persistence_test_agent_stopped",
                                      [this](const fidl::String&) {
                                        module_host_->module_context()->Done();
                                      });
  }

  modular::ModuleHost* module_host_;
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

  fxl::WeakPtrFactory<ParentApp> weak_ptr_factory_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<ParentApp> driver(app_context.get(),
                                          [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
