// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/tests/triggers/trigger_test_agent_interface.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : public modular::SingleServiceApp<modular::Agent>,
                     modular::testing::TriggerAgentInterface {
 public:
  TestAgentApp() { modular::testing::Init(application_context(), __FILE__); }

  ~TestAgentApp() override { mtl::MessageLoop::GetCurrent()->PostQuitTask(); }

 private:
  // |Agent|
  void Initialize(
      fidl::InterfaceHandle<modular::AgentContext> agent_context) override {
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
  }

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    agent_services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("trigger_test_agent_connected", "",
                                      [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const RunTaskCallback& callback) override {
    modular::testing::GetStore()->Put("trigger_test_agent_run_task", "",
                                      [callback] { callback(); });
  }

  // |Agent|
  void Stop(const StopCallback& callback) override {
    modular::testing::GetStore()->Put("trigger_test_agent_stopped", "", [] {});
    TEST_PASS("Trigger test agent exited");
    modular::testing::Done();
    callback();
    delete this;
  }

  // |TriggerAgentInterface|
  void GetMessageQueueToken(
      const GetMessageQueueTokenCallback& callback) override {
    msg_queue_->GetToken(
        [callback](const fidl::String& token) { callback(token); });
  }

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  modular::ServiceProviderImpl agent_services_;
  fidl::BindingSet<modular::testing::TriggerAgentInterface>
      trigger_agent_interface_;

  TestPoint initialized_{"Trigger test agent initialized"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  new TestAgentApp();
  loop.Run();
  return 0;
}
