// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class TestAgentApp : public modular::SingleServiceApp<modular::Agent> {
 public:
  TestAgentApp() { modular::testing::Init(application_context(), __FILE__); }

  ~TestAgentApp() override { mtl::MessageLoop::GetCurrent()->PostQuitTask(); }

 private:
  // |Agent|
  void Initialize(
      fidl::InterfaceHandle<modular::AgentContext> agent_context) override {}

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    modular::testing::GetStore()->Put("test_agent2_connected", "", [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const fidl::String& params,
               const RunTaskCallback& callback) override {}

  // |Agent|
  void Stop(const StopCallback& callback) override {
    TEST_PASS("Test agent2 exited");
    modular::testing::Done();
    callback();
    delete this;
  }
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  new TestAgentApp();
  loop.Run();
  return 0;
}
