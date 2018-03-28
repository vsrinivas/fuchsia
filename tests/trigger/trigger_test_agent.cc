// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/modular_test_trigger.h>
#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : modular_test_trigger::TriggerTestService {
 public:
  TestPoint initialized_{"Trigger test agent initialized"};

  TestAgentApp(modular::AgentHost* const agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Trigger Queue",
                                           msg_queue_.NewRequest());
    modular::TaskInfo task_info;
    task_info.task_id = "task_id";
    task_info.trigger_condition.set_queue_name("Trigger Queue");
    task_info.persistent = true;
    agent_host->agent_context()->ScheduleTask(std::move(task_info));

    agent_services_.AddService<modular_test_trigger::TriggerTestService>(
        [this](fidl::InterfaceRequest<modular_test_trigger::TriggerTestService>
                   request) {
          service_bindings_.AddBinding(this, std::move(request));
        });

    initialized_.Pass();
  }

  // Called by AgentDriver.
  void Connect(fidl::InterfaceRequest<component::ServiceProvider> services) {
    agent_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("trigger_test_agent_connected", "",
                                      [] {});
  }

  // Called by AgentDriver.
  void RunTask(fidl::StringPtr /*task_id*/,
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
  // |TriggerTestService|
  void GetMessageQueueToken(GetMessageQueueTokenCallback callback) override {
    msg_queue_->GetToken(
        [callback](fidl::StringPtr token) { callback(token); });
  }

  component::ServiceNamespace agent_services_;

  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  fidl::BindingSet<modular_test_trigger::TriggerTestService> service_bindings_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<TestAgentApp> driver(app_context.get(),
                                            [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
