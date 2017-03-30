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

class UnstoppableAgentApp : public modular::SingleServiceApp<modular::Agent> {
 public:
  UnstoppableAgentApp() {
    modular::testing::Init(application_context(), __FILE__);
  }

  ~UnstoppableAgentApp() override {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

 private:
  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_context_.Bind(std::move(agent_context));
    agent_context_->GetComponentContext(component_context_.NewRequest());
    initialized_.Pass();
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const RunTaskCallback& callback) override {}

  // |Agent|
  void Stop(const StopCallback& callback) override {
    modular::testing::WillTerminate(5);
    callback();
    stopped_.Pass();
  }

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;

  TestPoint initialized_{"Unstoppable module initialized"};
  TestPoint stopped_{"Unstoppable module stopped"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  new UnstoppableAgentApp();
  loop.Run();
  return 0;
}
