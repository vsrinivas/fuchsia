// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/component/cpp/connect.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <lib/message_queue/cpp/message_sender_client.h>
#include <test/peridot/tests/queuepersistence/cpp/fidl.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/queue_persistence/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;
using namespace test::peridot::tests::queuepersistence;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), weak_ptr_factory_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    module_host_->module_context()->GetComponentContext(
        component_context_.NewRequest());

    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    component::ConnectToService(agent_services.get(),
                                agent_service_.NewRequest());

    Await("queue_persistence_test_agent_connected",
          [this] { AgentConnected(); });
  }

  TestPoint stopped_{"Root module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint agent_connected_{"fuchsia::modular::Agent accepted connection"};

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

  TestPoint agent_stopped_{"fuchsia::modular::Agent stopped"};

  void AgentStopped() {
    agent_stopped_.Pass();

    // Send a message to the stopped agent which should be persisted to local
    // storage. No triggers are set so the agent won't be automatically started.
    modular::MessageSenderClient message_sender;
    component_context_->GetMessageSender(queue_token_,
                                         message_sender.NewRequest());
    message_sender.Send("Queued message...");

    // Start the agent again.
    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    component::ConnectToService(agent_services.get(),
                                agent_service_.NewRequest());

    Await("queue_persistence_test_agent_connected",
          [this] { AgentConnectedAgain(); });
  }

  TestPoint agent_connected_again_{
      "fuchsia::modular::Agent accepted connection, again"};

  void AgentConnectedAgain() {
    agent_connected_again_.Pass();
    Await("queue_persistence_test_agent_received_message",
          [this] { AgentReceivedMessage(); });
  }

  TestPoint agent_received_message_{"fuchsia::modular::Agent received message"};

  void AgentReceivedMessage() {
    agent_received_message_.Pass();

    // Stop the agent again.
    agent_controller_.Unbind();
    agent_service_.Unbind();
    Await("queue_persistence_test_agent_stopped",
          [this] { Signal(modular::testing::kTestShutdown); });
  }

  modular::ModuleHost* module_host_;
  fuchsia::modular::AgentControllerPtr agent_controller_;
  QueuePersistenceTestServicePtr agent_service_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::MessageQueuePtr msg_queue_;

  std::string queue_token_;

  fxl::WeakPtrFactory<TestApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
