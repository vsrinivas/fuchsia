// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/fidl/agent.fidl.h"
#include "lib/agent_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp {
 public:
  TestAgentApp(modular::AgentHost* agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
  }

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<app::ServiceProvider> /*services*/) {
    modular::testing::GetStore()->Put("test_agent2_connected", "", [] {});
  }

  // Called by AgentDriver.
  void RunTask(const fidl::String& /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    terminate_called_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint terminate_called_{"Terminate() called."};
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
