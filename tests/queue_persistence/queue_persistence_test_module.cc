// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <queue_persistence_test_service/cpp/fidl.h>

#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/callback/scoped_callback.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/queue_persistence/defs.h"

using fuchsia::modular::testing::Await;
using fuchsia::modular::testing::Signal;
using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestApp(fuchsia::modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), weak_ptr_factory_(this) {
    fuchsia::modular::testing::Init(module_host->application_context(),
                                    __FILE__);
    initialized_.Pass();

    module_host_->module_context()->GetComponentContext(
        component_context_.NewRequest());

    component::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(), agent_service_.NewRequest());

    Await("queue_persistence_test_agent_connected",
          [this] { AgentConnected(); });
  }

  TestPoint stopped_{"Root module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    fuchsia::modular::testing::Done(done);
  }

 private:
  TestPoint agent_connected_{"Agent accepted connection"};

  void AgentConnected() {
    agent_connected_.Pass();
    agent_service_->GetMessageQueueToken(
        [this](const fidl::StringPtr& token) { ReceivedQueueToken(token); });
  }

  TestPoint received_queue_persistence_token_{
      "Received queue_persistence token"};

  void ReceivedQueueToken(const fidl::StringPtr& token) {
    queue_token_ = token;
    received_queue_persistence_token_.Pass();

    // Stop the agent.
    agent_controller_.Unbind();
    agent_service_.Unbind();
    Await("queue_persistence_test_agent_stopped", [this] { AgentStopped(); });
  }

  TestPoint agent_stopped_{"Agent stopped"};

  void AgentStopped() {
    agent_stopped_.Pass();

    // Send a message to the stopped agent which should be persisted to local
    // storage. No triggers are set so the agent won't be automatically started.
    fuchsia::modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(queue_token_,
                                         message_sender.NewRequest());
    message_sender->Send("Queued message...");

    // Start the agent again.
    component::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(), agent_service_.NewRequest());

    Await("queue_persistence_test_agent_connected",
          [this] { AgentConnectedAgain(); });
  }

  TestPoint agent_connected_again_{"Agent accepted connection, again"};

  void AgentConnectedAgain() {
    agent_connected_again_.Pass();
    Await("queue_persistence_test_agent_received_message",
          [this] { AgentReceivedMessage(); });
  }

  TestPoint agent_received_message_{"Agent received message"};

  void AgentReceivedMessage() {
    agent_received_message_.Pass();

    // Stop the agent again.
    agent_controller_.Unbind();
    agent_service_.Unbind();
    Await("queue_persistence_test_agent_stopped",
          [this] { Signal(fuchsia::modular::testing::kTestShutdown); });
  }

  fuchsia::modular::ModuleHost* module_host_;
  fuchsia::modular::AgentControllerPtr agent_controller_;
  queue_persistence_test_service::QueuePersistenceTestServicePtr agent_service_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::MessageQueuePtr msg_queue_;

  std::string queue_token_;

  fxl::WeakPtrFactory<TestApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<TestApp> driver(app_context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
