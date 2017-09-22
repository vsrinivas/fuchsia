// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "peridot/tests/component_context/test_agent1_interface.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

constexpr char kTest2Agent[] =
    "file:///system/apps/modular_tests/component_context_test_agent2";

class TestAgentApp : modular::testing::ComponentBase<modular::Agent>,
                     modular::testing::Agent1Interface {
 public:
  static void New() { new TestAgentApp; }

 private:
  TestAgentApp() { TestInit(__FILE__); }
  ~TestAgentApp() override = default;

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_context_.Bind(std::move(agent_context));
    agent_context_->GetComponentContext(component_context_.NewRequest());
    agent1_services_.AddService<modular::testing::Agent1Interface>(
        [this](fidl::InterfaceRequest<modular::testing::Agent1Interface>
                   interface_request) {
          agent1_interface_.AddBinding(this, std::move(interface_request));
        });

    // Connecting to the agent should start it up.
    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTest2Agent, agent_services.NewRequest(),
                                       agent2_controller_.NewRequest());
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& /*requestor_url*/,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    agent1_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("test_agent1_connected", "", [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& /*task_id*/,
               const RunTaskCallback& /*callback*/) override {}

  // |Lifecycle|
  void Terminate() override {
    // Before reporting that we stop, we wait until agent2 has connected.
    modular::testing::GetStore()->Get(
        "test_agent2_connected", [this](const fidl::String&) {
          // Killing the agent controller should stop it.
          agent2_controller_.reset();
          agent2_connected_.Pass();
          modular::testing::GetStore()->Put(
              "test_agent1_stopped", "", [this] { DeleteAndQuitAndUnbind(); });
        });
  }

  // |Agent1Interface|
  void SendToMessageQueue(const fidl::String& message_queue_token,
                          const fidl::String& message_to_send) override {
    modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(message_queue_token,
                                         message_sender.NewRequest());

    message_sender->Send(message_to_send);
  }

  TestPoint agent2_connected_{"Test agent2 accepted connection"};

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;
  modular::AgentControllerPtr agent2_controller_;

  app::ServiceNamespace agent1_services_;
  fidl::BindingSet<modular::testing::Agent1Interface> agent1_interface_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestAgentApp::New();
  loop.Run();
  return 0;
}
