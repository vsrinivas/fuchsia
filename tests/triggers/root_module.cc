// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/connect.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/tests/triggers/trigger_test_agent_interface.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 10000;
constexpr char kTestAgent[] =
    "file:///system/apps/modular_tests/trigger_test_agent";

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

    // Exercise ComponentContext.ConnectToAgent()
    module_context_->GetComponentContext(component_context_.NewRequest());

    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTestAgent, agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(),
                     trigger_agent_interface_.NewRequest());

    modular::testing::GetStore()->Get(
        "trigger_test_agent_connected", [this](const fidl::String&) {
          agent_connected_.Pass();
          trigger_agent_interface_->GetMessageQueueToken(
              [this](const fidl::String& token) {
                received_trigger_token_.Pass();

                // Stop the agent.
                agent_controller_.reset();
                modular::testing::GetStore()->Get(
                    "trigger_test_agent_stopped",
                    [this, token](const fidl::String&) {
                      agent_stopped_.Pass();

                      // Send a message to the stopped agent which should
                      // trigger it.
                      modular::MessageSenderPtr message_sender;
                      component_context_->GetMessageSender(
                          token, message_sender.NewRequest());
                      message_sender->Send("Time to wake up...");

                      modular::testing::GetStore()->Get(
                          "trigger_test_agent_run_task",
                          [this](const fidl::String&) {
                            task_triggered_.Pass();

                            modular::testing::GetStore()->Get(
                                "trigger_test_agent_stopped",
                                [this](const fidl::String&) {
                                  module_context_->Done();
                                });
                          });

                    });

              });
        });

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { module_context_->Done(); }),
        ftl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  modular::ModuleContextPtr module_context_;
  modular::AgentControllerPtr agent_controller_;
  modular::testing::TriggerAgentInterfacePtr trigger_agent_interface_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  TestPoint initialized_{"Root module initialized"};
  TestPoint received_trigger_token_{"Received trigger token"};
  TestPoint stopped_{"Root module stopped"};
  TestPoint agent_connected_{"Agent accepted connection"};
  TestPoint agent_stopped_{"Agent1 stopped"};
  TestPoint task_triggered_{"Agent task triggered"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
