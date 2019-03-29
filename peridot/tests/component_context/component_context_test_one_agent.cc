// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/logging.h>
#include <lib/message_queue/cpp/message_sender_client.h>
#include <test/peridot/tests/componentcontext/cpp/fidl.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/component_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::componentcontext::ComponentContextTestService;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : ComponentContextTestService {
 public:
  TestApp(modular::AgentHost* const agent_host) {
    modular::testing::Init(agent_host->startup_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());
    agent_services_.AddService<ComponentContextTestService>(
        [this](fidl::InterfaceRequest<ComponentContextTestService> request) {
          agent_interface_.AddBinding(this, std::move(request));
        });

    // Connecting to the agent should start it up.
    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kTwoAgentUrl,
                                       agent_services.NewRequest(),
                                       two_agent_controller_.NewRequest());
  }

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
    agent_services_.AddBinding(std::move(request));
    Signal("one_agent_connected");
  }

  // Called by AgentDriver.
  void RunTask(const fidl::StringPtr& /*task_id*/,
               fit::function<void()> /*callback*/) {}

  TestPoint two_agent_connected_{"Two agent accepted connection"};

  // Called by AgentDriver.
  void Terminate(fit::function<void()> done) {
    FXL_LOG(INFO) << "TestOneAgent::Terminate()";
    // Before reporting that we stop, we wait until two_agent has connected.
    Await("two_agent_connected", [this, done = std::move(done)]() mutable {
      FXL_LOG(INFO) << "TestOneAgent::Terminate() GET";
      // Killing the agent controller should stop it.
      two_agent_controller_.Unbind();
      two_agent_connected_.Pass();
      Signal("one_agent_stopped");
      modular::testing::Done(std::move(done));
    });
  }

 private:
  // |Agent1Interface|
  void SendToMessageQueue(std::string message_queue_token,
                          std::string message_to_send) override {
    modular::MessageSenderClient message_sender;
    component_context_->GetMessageSender(message_queue_token,
                                         message_sender.NewRequest());
    message_sender.Send(message_to_send);
  }

  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::AgentControllerPtr two_agent_controller_;

  component::ServiceNamespace agent_services_;
  fidl::BindingSet<ComponentContextTestService> agent_interface_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AgentDriver<TestApp> driver(context.get(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
