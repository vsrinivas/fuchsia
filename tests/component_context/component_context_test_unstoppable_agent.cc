// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/component_context/defs.h"

using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Unstoppable agent initialized"};

  TestApp(fuchsia::modular::AgentHost* agent_host) {
    fuchsia::modular::testing::Init(agent_host->startup_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());
    initialized_.Pass();
  }

  // Called by AgentDriver.
  void Connect(
      fidl::InterfaceRequest<component::ServiceProvider> /*services*/) {}

  // Called by AgentDriver.
  void RunTask(fidl::StringPtr /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  TestPoint stopped_{"Unstoppable agent stopped"};

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    fuchsia::modular::testing::Done(done);
  }

 private:
  fuchsia::modular::AgentContextPtr agent_context_;
  fuchsia::modular::ComponentContextPtr component_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto context = component::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::AgentDriver<TestApp> driver(context.get(),
                                                [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
