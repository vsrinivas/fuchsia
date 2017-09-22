// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/tests/triggers/trigger_test_agent_interface.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : modular::testing::ComponentBase<modular::Agent>,
                     modular::testing::TriggerAgentInterface {
 public:
  static void New() { new TestAgentApp; }

 private:
  TestAgentApp() { TestInit(__FILE__); }
  ~TestAgentApp() override = default;

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_context_.Bind(std::move(agent_context));
    agent_context_->GetComponentContext(component_context_.NewRequest());

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Trigger Queue",
                                           msg_queue_.NewRequest());
    auto task_info = modular::TaskInfo::New();
    task_info->task_id = "task_id";
    auto trigger_condition = modular::TriggerCondition::New();
    trigger_condition->set_queue_name("Trigger Queue");
    task_info->trigger_condition = std::move(trigger_condition);
    agent_context_->ScheduleTask(std::move(task_info));

    agent_services_.AddService<modular::testing::TriggerAgentInterface>(
        [this](fidl::InterfaceRequest<modular::testing::TriggerAgentInterface>
                   interface_request) {
          trigger_agent_interface_.AddBinding(this,
                                              std::move(interface_request));
        });

    initialized_.Pass();
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& /*requestor_url*/,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    agent_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("trigger_test_agent_connected", "",
                                      [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& /*task_id*/,
               const RunTaskCallback& callback) override {
    modular::testing::GetStore()->Put("trigger_test_agent_run_task", "",
                                      [callback] { callback(); });
  }

  // |Lifecycle|
  void Terminate() override {
    modular::testing::GetStore()->Put("trigger_test_agent_stopped", "",
                                      [this] { DeleteAndQuitAndUnbind(); });
  }

  // |TriggerAgentInterface|
  void GetMessageQueueToken(
      const GetMessageQueueTokenCallback& callback) override {
    msg_queue_->GetToken(
        [callback](const fidl::String& token) { callback(token); });
  }

  app::ServiceNamespace agent_services_;

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  fidl::BindingSet<modular::testing::TriggerAgentInterface>
      trigger_agent_interface_;

  TestPoint initialized_{"Trigger test agent initialized"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestAgentApp::New();
  loop.Run();
  return 0;
}
