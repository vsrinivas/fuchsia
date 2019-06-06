// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/component/cpp/connect.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <lib/message_queue/cpp/message_queue_client.h>
#include <test/peridot/tests/componentcontext/cpp/fidl.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/component_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::componentcontext::ComponentContextTestServicePtr;

namespace {

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestModule(modular::ModuleHost* module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : weak_ptr_factory_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);

    initialized_.Pass();

    // Exercise fuchsia::modular::ComponentContext.ConnectToAgent()
    module_host->module_context()->GetComponentContext(
        component_context_.NewRequest());

    fuchsia::sys::ServiceProviderPtr one_agent_services;
    component_context_->ConnectToAgent(kOneAgentUrl,
                                       one_agent_services.NewRequest(),
                                       one_agent_controller.NewRequest());
    component::ConnectToService(one_agent_services.get(),
                                one_agent_interface_.NewRequest());

    TestAgentConnected();
  }

  // Called by ModuleDriver.
  TestPoint stopped_{"Root module stopped"};
  void Terminate(fit::function<void()> done) {
    stopped_.Pass();
    modular::testing::Done(std::move(done));
  }

 private:
  TestPoint one_agent_connected_{"One agent accepted connection"};

  void TestAgentConnected() {
    Await("one_agent_connected", [this] {
      FXL_LOG(INFO) << "TestModule One Agent Connected";
      one_agent_connected_.Pass();
      TestMessageQueue();
    });
  }

  TestPoint msg_queue_communicated_{
      "Communicated message between Agent one using a MessageQueue"};

  // Tests message queues.
  void TestMessageQueue() {
    constexpr char kTestMessage[] = "test message!";

    component_context_->ObtainMessageQueue("root_msg_queue",
                                           msg_queue_.NewRequest());

    // MessageQueueManager shouldn't send us anything just yet.
    msg_queue_.RegisterReceiver(
        [this, kTestMessage](const std::string& msg,
                             fit::function<void()> ack) {
          ack();

          if (msg == kTestMessage) {
            msg_queue_communicated_.Pass();
          }

          TestAgentController();
        });

    msg_queue_.GetToken([this, kTestMessage](const fidl::StringPtr& token) {
      one_agent_interface_->SendToMessageQueue(token, kTestMessage);
    });
  }

  TestPoint one_agent_stopped_{"One agent stopped"};

  // Tests fuchsia::modular::AgentController. Calls |done_cb| when completed
  // successfully.
  void TestAgentController() {
    // Closing the agent controller should trigger the agent to stop.
    one_agent_controller.Unbind();

    Await("one_agent_stopped", [this] {
      one_agent_stopped_.Pass();
      TestUnstoppableAgent();
    });
  }

  // Start an agent that will not stop of its own accord.
  void TestUnstoppableAgent() {
    fuchsia::sys::ServiceProviderPtr unstoppable_agent_services;
    component_context_->ConnectToAgent(
        kUnstoppableAgent, unstoppable_agent_services.NewRequest(),
        unstoppable_agent_controller_.NewRequest());

    // After 500ms close the fuchsia::modular::AgentController for the
    // unstoppable agent.
    // TODO(jimbe) We don't check if the agent started running in the allotted
    // time, so this test isn't reliable. We need to make a call to the agent
    // and wait for a response.
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                             [this] {
                               unstoppable_agent_controller_.Unbind();
                               Signal(modular::testing::kTestShutdown);
                             }),
        zx::msec(500));
  }

  fuchsia::modular::AgentControllerPtr one_agent_controller;
  ComponentContextTestServicePtr one_agent_interface_;
  fuchsia::modular::ComponentContextPtr component_context_;
  modular::MessageQueueClient msg_queue_;

  fuchsia::modular::AgentControllerPtr unstoppable_agent_controller_;

  fxl::WeakPtrFactory<TestModule> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
