// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/fidl/agent.fidl.h"
#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/lifecycle/fidl/lifecycle.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/triggers/trigger_test_agent_interface.fidl.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : modular::testing::TriggerAgentInterface {
 public:
  TestAgentApp(modular::AgentHost* agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Trigger Queue",
                                           msg_queue_.NewRequest());
    auto task_info = modular::TaskInfo::New();
    task_info->task_id = "task_id";
    auto trigger_condition = modular::TriggerCondition::New();
    trigger_condition->set_queue_name("Trigger Queue");
    task_info->trigger_condition = std::move(trigger_condition);
    agent_host->agent_context()->ScheduleTask(std::move(task_info));

    agent_services_.AddService<modular::testing::TriggerAgentInterface>(
        [this](fidl::InterfaceRequest<modular::testing::TriggerAgentInterface>
                   interface_request) {
          trigger_agent_interface_.AddBinding(this,
                                              std::move(interface_request));
        });

    initialized_.Pass();
  }

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<app::ServiceProvider> services) {
    agent_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("trigger_test_agent_connected", "",
                                      [] {});
  }

  // Called by AgentDriver.
  void RunTask(const fidl::String& /*task_id*/,
               const std::function<void()>& callback) {
    modular::testing::GetStore()->Put("trigger_test_agent_run_task", "",
                                      callback);
  }

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("trigger_test_agent_stopped", "",
                                      [done] { modular::testing::Done(done); });
  }

 private:
  // |TriggerAgentInterface|
  void GetMessageQueueToken(
      const GetMessageQueueTokenCallback& callback) override {
    msg_queue_->GetToken(
        [callback](const fidl::String& token) { callback(token); });
  }

  app::ServiceNamespace agent_services_;

  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  fidl::BindingSet<modular::testing::TriggerAgentInterface>
      trigger_agent_interface_;

  TestPoint initialized_{"Trigger test agent initialized"};
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
