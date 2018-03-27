// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>
#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/component_context/component_context_test_service.fidl.h"

using modular::testing::TestPoint;

namespace {

constexpr char kTwoAgentUrl[] =
    "file:///system/test/modular_tests/component_context_test_two_agent";

class TestAgentApp : modular::ComponentContextTestService {
 public:
  TestAgentApp(modular::AgentHost* const agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());
    agent_services_.AddService<modular::ComponentContextTestService>(
        [this](f1dl::InterfaceRequest<modular::ComponentContextTestService>
                   request) {
          agent_interface_.AddBinding(this, std::move(request));
        });

    // Connecting to the agent should start it up.
    component::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTwoAgentUrl,
                                       agent_services.NewRequest(),
                                       two_agent_controller_.NewRequest());
  }

  // Called by AgentDriver.
  void Connect(f1dl::InterfaceRequest<component::ServiceProvider> request) {
    agent_services_.AddBinding(std::move(request));
    modular::testing::GetStore()->Put("one_agent_connected", "", [] {});
  }

  // Called by AgentDriver.
  void RunTask(const f1dl::StringPtr& /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  TestPoint two_agent_connected_{"Two agent accepted connection"};

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    // Before reporting that we stop, we wait until two_agent has connected.
    modular::testing::GetStore()->Get(
        "two_agent_connected", [this, done](const f1dl::StringPtr&) {
          // Killing the agent controller should stop it.
          two_agent_controller_.Unbind();
          two_agent_connected_.Pass();
          modular::testing::GetStore()->Put("one_agent_stopped", "", [done] {
            modular::testing::Done(done);
          });
        });
  }

 private:
  // |Agent1Interface|
  void SendToMessageQueue(const f1dl::StringPtr& message_queue_token,
                          const f1dl::StringPtr& message_to_send) override {
    modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(message_queue_token,
                                         message_sender.NewRequest());

    message_sender->Send(message_to_send);
  }

  modular::ComponentContextPtr component_context_;
  modular::AgentControllerPtr two_agent_controller_;

  component::ServiceNamespace agent_services_;
  f1dl::BindingSet<modular::ComponentContextTestService> agent_interface_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<TestAgentApp> driver(app_context.get(),
                                            [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
