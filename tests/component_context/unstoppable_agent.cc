// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/fidl/agent.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class UnstoppableAgentApp : modular::testing::ComponentBase<modular::Agent> {
 public:
  static void New() {
    new UnstoppableAgentApp;  // Deleted in Stop().
  }

 private:
  UnstoppableAgentApp() { TestInit(__FILE__); }
  ~UnstoppableAgentApp() override = default;

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_context_.Bind(std::move(agent_context));
    agent_context_->GetComponentContext(component_context_.NewRequest());
    initialized_.Pass();
    callback();
  }

  // |Agent|
  void Connect(
      const fidl::String& /*requestor_url*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {}

  // |Agent|
  void RunTask(const fidl::String& /*task_id*/,
               const RunTaskCallback& /*callback*/) override {}

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    Delete([] {});  // We don't post Quit here.
  }

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;

  TestPoint initialized_{"Unstoppable agent initialized"};
  TestPoint stopped_{"Unstoppable agent stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  UnstoppableAgentApp::New();
  loop.Run();  // Never returns.
  return 0;
}
