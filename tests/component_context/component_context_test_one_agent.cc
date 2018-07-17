// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/agent_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/logging.h>
#include <test/peridot/tests/componentcontext/cpp/fidl.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/component_context/defs.h"

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
    modular::testing::GetStore()->Put("one_agent_connected", "", [] {});
  }

  // Called by AgentDriver.
  void RunTask(const fidl::StringPtr& /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  TestPoint two_agent_connected_{"Two agent accepted connection"};

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    // Before reporting that we stop, we wait until two_agent has connected.
    modular::testing::GetStore()->Get(
        "two_agent_connected", [this, done](const fidl::StringPtr&) {
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
  void SendToMessageQueue(fidl::StringPtr message_queue_token,
                          fidl::StringPtr message_to_send) override {
    fuchsia::modular::MessageSenderPtr message_sender;
    component_context_->GetMessageSender(message_queue_token,
                                         message_sender.NewRequest());

    message_sender->Send(message_to_send);
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
