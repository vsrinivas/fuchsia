// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/message_queue/cpp/message_sender_client.h>
#include <test/peridot/tests/trigger/cpp/fidl.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/trigger/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::trigger::TriggerTestServicePtr;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    // Exercise fuchsia::modular::ComponentContext.ConnectToAgent()
    module_host->module_context()->GetComponentContext(
        component_context_.NewRequest());

    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    component::ConnectToService(agent_services.get(), agent_service_.NewRequest());

    // The message queue that is used to verify deletion triggers from explicit
    // deletes.
    component_context_->ObtainMessageQueue("explicit_test",
                                           explicit_msg_queue_.NewRequest());
    // The message queue that is used to verify deletion triggers from deletes
    // when the module's namespace is torn down. The test user shell will
    // verify that the agent is notified of this queues deletion.
    component_context_->ObtainMessageQueue("implicit_test",
                                           implicit_msg_queue_.NewRequest());
    implicit_msg_queue_->GetToken([this](fidl::StringPtr token) {
      modular::testing::GetStore()->Put("trigger_test_module_queue_token",
                                        token, [] {});
      agent_service_->ObserveMessageQueueDeletion(token);
      explicit_msg_queue_->GetToken([this](fidl::StringPtr token) {
        explicit_queue_token_ = token;
        agent_service_->ObserveMessageQueueDeletion(std::move(token));

        TestMessageQueueMessageTrigger();
      });
    });
  }

  TestPoint received_trigger_token_{"Received trigger token"};
  TestPoint agent_connected_{"fuchsia::modular::Agent accepted connection"};
  TestPoint agent_stopped_{"fuchsia::modular::Agent stopped"};
  TestPoint task_triggered_{"fuchsia::modular::Agent task triggered"};
  void TestMessageQueueMessageTrigger() {
    Await("trigger_test_agent_connected", [this] {
      agent_connected_.Pass();
      agent_service_->GetMessageQueueToken([this](fidl::StringPtr token) {
        received_trigger_token_.Pass();

        // Stop the agent.
        agent_controller_.Unbind();

        Await("trigger_test_agent_stopped", [this, token] {
          agent_stopped_.Pass();

          // Send a message to the stopped agent which should
          // trigger it.
          component_context_->GetMessageSender(token,
                                               message_sender_.NewRequest());
          message_sender_.Send("Time to wake up...");

          Await("message_queue_message", [this] {
            task_triggered_.Pass();
            Await("trigger_test_agent_stopped",
                  [this] { TestMessageQueueDeletionTrigger(); });
          });
        });
      });
    });
  }

  TestPoint queue_deleted_{"Message queue deletion task triggered."};
  void TestMessageQueueDeletionTrigger() {
    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    component::ConnectToService(agent_services.get(), agent_service_.NewRequest());

    // First wait for the agent to connect, and then kill it.
    Await("trigger_test_agent_connected", [this] {
      Await("trigger_test_agent_token_received", [this] {
        agent_controller_.Unbind();
        Await("trigger_test_agent_stopped", [this] {
          // When the agent has stopped, delete the message queue and verify
          // that the agent is woken up and notified.
          component_context_->DeleteMessageQueue("explicit_test");
          Await(explicit_queue_token_, [this] {
            queue_deleted_.Pass();
            Signal("trigger_test_module_done");
          });
        });
      });
    });
  }

  TestPoint stopped_{"Root module stopped"};
  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  fuchsia::modular::AgentControllerPtr agent_controller_;
  TriggerTestServicePtr agent_service_;
  fuchsia::modular::ComponentContextPtr component_context_;
  // The queue used for observing explicit queue deletion.
  fuchsia::modular::MessageQueuePtr explicit_msg_queue_;
  std::string explicit_queue_token_;
  // The queue used for observing queue deletion when module's namespace is torn
  // down.
  fuchsia::modular::MessageQueuePtr implicit_msg_queue_;
  modular::MessageSenderClient message_sender_;

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
