// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/message_receiver_client.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "lib/agent/fidl/agent.fidl.h"
#include "lib/component/fidl/message_queue.fidl.h"
#include "peridot/tests/queue_persistence/queue_persistence_test_agent_interface.fidl.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace {

class TestAgentApp : modular::testing::ComponentBase<modular::Agent>,
                     modular::testing::QueuePersistenceAgentInterface {
 public:
  static void New() {
    new TestAgentApp;  // deleted in Stop().
  }

 private:
  using TestPoint = modular::testing::TestPoint;

  TestAgentApp() { TestInit(__FILE__); }
  ~TestAgentApp() override = default;

  TestPoint initialized_{"Queue persistence test agent initialized"};

  // |Agent|
  void Initialize(fidl::InterfaceHandle<modular::AgentContext> agent_context,
                  const InitializeCallback& callback) override {
    agent_context_.Bind(std::move(agent_context));
    agent_context_->GetComponentContext(component_context_.NewRequest());

    // Create a message queue and schedule a task to be run on receiving a
    // message on it.
    component_context_->ObtainMessageQueue("Test Queue",
                                           msg_queue_.NewRequest());
    msg_receiver_ = std::make_unique<modular::MessageReceiverClient>(
        msg_queue_.get(),
        [this](const fidl::String& message, std::function<void()> ack) {
          ack();
          modular::testing::GetStore()->Put(
              "queue_persistence_test_agent_received_message", "", [] {});
        });

    services_.AddService<modular::testing::QueuePersistenceAgentInterface>(
        [this](fidl::InterfaceRequest<
               modular::testing::QueuePersistenceAgentInterface> request) {
          services_bindings_.AddBinding(this, std::move(request));
        });

    initialized_.Pass();
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& /*requestor_url*/,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("queue_persistence_test_agent_connected",
                                      "", [] {});
  }

  // |Agent|
  void RunTask(const fidl::String& /*task_id*/,
               const RunTaskCallback& /*callback*/) override {}

  // |Lifecycle|
  void Terminate() override {
    // Stop processing messages, since we do async operations below and don't
    // want our receiver to fire.
    msg_receiver_.reset();

    modular::testing::GetStore()->Put("queue_persistence_test_agent_stopped",
                                      "", [this] { DeleteAndQuitAndUnbind(); });
  }

  // |QueuePersistenceAgentInterface|
  void GetMessageQueueToken(
      const GetMessageQueueTokenCallback& callback) override {
    msg_queue_->GetToken(
        [callback](const fidl::String& token) { callback(token); });
  }

  modular::AgentContextPtr agent_context_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  std::unique_ptr<modular::MessageReceiverClient> msg_receiver_;

  app::ServiceNamespace services_;
  fidl::BindingSet<modular::testing::QueuePersistenceAgentInterface>
      services_bindings_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  TestAgentApp::New();
  loop.Run();
  return 0;
}
