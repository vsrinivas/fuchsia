// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : public modular::SingleServiceApp<modular::Agent> {
 public:
  TestAgentApp() { modular::testing::Init(application_context()); }

  ~TestAgentApp() override {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

 private:
  // |Agent|
  void Initialize(
      fidl::InterfaceHandle<modular::AgentContext> agent_context) override {
    initialized_.Pass();
  }

  // |Agent|
  void Connect(
      const fidl::String& requestor_url,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    connected_.Pass();
    modular::testing::GetStore()->Put("test_agent_connected", "", [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const fidl::String& params,
               const RunTaskCallback& callback) override {}

  // |Agent|
  void Stop(const StopCallback& callback) override {
    modular::testing::GetStore()->Put("test_agent_stopped", "", [] {});
    callback();
    delete this;
  }

  TestPoint initialized_{"Test agent initialized"};
  TestPoint connected_{"Test agent received connection"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  new TestAgentApp();
  loop.Run();
  TEST_PASS("Test agent exited");
  modular::testing::Done();
  return 0;
}
