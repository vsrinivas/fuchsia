// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/fidl/agent.fidl.h"
#include "lib/app_driver/cpp/agent_driver.h"
#include "lib/component/fidl/message_queue.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/fidl/message_receiver_client.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/queue_persistence/queue_persistence_test_service.fidl.h"

using modular::testing::TestPoint;

namespace {

class TestAgentApp : modular::QueuePersistenceTestService {
 public:
  TestAgentApp(modular::AgentHost* agent_host) {
    modular::testing::Init(agent_host->application_context(), __FILE__);
    agent_host->agent_context()->GetComponentContext(
        component_context_.NewRequest());

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Test Queue",
                                           msg_queue_.NewRequest());
    msg_receiver_ = std::make_unique<modular::MessageReceiverClient>(
        msg_queue_.get(),
        [this](const f1dl::StringPtr& message, std::function<void()> ack) {
          ack();
          modular::testing::GetStore()->Put(
              "queue_persistence_test_agent_received_message", "", [] {});
        });

    services_.AddService<modular::QueuePersistenceTestService>(
        [this](f1dl::InterfaceRequest<modular::QueuePersistenceTestService>
                   request) {
          services_bindings_.AddBinding(this, std::move(request));
        });

    initialized_.Pass();
  }

  // Called by AgentDriver.
  void Connect(f1dl::InterfaceRequest<component::ServiceProvider> services) {
    services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("queue_persistence_test_agent_connected",
                                      "", [] {});
  }

  // Called by AgentDriver.
  void RunTask(const f1dl::StringPtr& /*task_id*/,
               const std::function<void()>& /*callback*/) {}

  // Called by AgentDriver.
  void Terminate(const std::function<void()>& done) {
    // Stop processing messages, since we do async operations below and don't
    // want our receiver to fire.
    msg_receiver_.reset();

    modular::testing::GetStore()->Put("queue_persistence_test_agent_stopped",
                                      "",
                                      [done] { modular::testing::Done(done); });
  }

 private:
  // |QueuePersistenceTestService|
  void GetMessageQueueToken(
      const GetMessageQueueTokenCallback& callback) override {
    msg_queue_->GetToken(
        [callback](const f1dl::StringPtr& token) { callback(token); });
  }

  TestPoint initialized_{"Queue persistence test agent initialized"};

  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  std::unique_ptr<modular::MessageReceiverClient> msg_receiver_;

  component::ServiceNamespace services_;
  f1dl::BindingSet<modular::QueuePersistenceTestService> services_bindings_;
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
