// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : modular::testing::ComponentBase<modular::Agent> {
 public:
  static void New() { new TestAgentApp; }

 private:
  TestAgentApp() { TestInit(__FILE__); }
  ~TestAgentApp() override = default;

  // |Agent|
  void Initialize(
      fidl::InterfaceHandle<modular::AgentContext> /*agent_context*/,
      const InitializeCallback& callback) override {
    callback();
  }

  // |Agent|
  void Connect(
      const fidl::String& /*requestor_url*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {
    modular::testing::GetStore()->Put("test_agent2_connected", "", [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& /*task_id*/,
               const RunTaskCallback& /*callback*/) override {}

  // |Lifecycle|
  void Terminate() override {
    terminate_called_.Pass();
    DeleteAndQuitAndUnbind();
  }

  TestPoint terminate_called_{"Terminate() called."};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestAgentApp::New();
  loop.Run();
  return 0;
}
