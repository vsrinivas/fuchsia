// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/fidl/agent.fidl.h"
#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/component_context/test_agent1_interface.fidl.h"

using modular::testing::TestPoint;

namespace {

constexpr char kTest2Agent[] =
    "file:///system/test/modular_tests/component_context_test_agent2";

class TestAgentApp : modular::testing::Agent1Interface {
 public:
  TestAgentApp(modular::AgentHost* agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());
    agent1_services_.AddService<modular::testing::Agent1Interface>(
        [this](fidl::InterfaceRequest<modular::testing::Agent1Interface>
                   interface_request) {
          agent1_interface_.AddBinding(this, std::move(interface_request));
        });

    // Connecting to the agent should start it up.
    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTest2Agent, agent_services.NewRequest(),
                                       agent2_controller_.NewRequest());
  }

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<app::ServiceProvider> services) {
    agent1_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("test_agent1_connected", "", [] {});
  }

  // Called by AgentDriver.
  void RunTask(const fidl::String& /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    // Before reporting that we stop, we wait until agent2 has connected.
    modular::testing::GetStore()->Get(
        "test_agent2_connected", [this, done](const fidl::String&) {
          // Killing the agent controller should stop it.
          agent2_controller_.reset();
          agent2_connected_.Pass();
          modular::testing::GetStore()->Put("test_agent1_stopped", "", [done] {
            modular::testing::Done(done);
          });
        });
  }

 private:
  // |Agent1Interface|
  void SendToMessageQueue(const fidl::String& message_queue_token,
                          const fidl::String& message_to_send) override {
    modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(message_queue_token,
                                         message_sender.NewRequest());

    message_sender->Send(message_to_send);
  }

  TestPoint agent2_connected_{"Test agent2 accepted connection"};

  modular::ComponentContextPtr component_context_;
  modular::AgentControllerPtr agent2_controller_;

  app::ServiceNamespace agent1_services_;
  fidl::BindingSet<modular::testing::Agent1Interface> agent1_interface_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<TestAgentApp> driver(app_context.get(),
                                            [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
