// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/tests/queue_persistence/queue_persistence_test_agent_interface.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class TestAgentApp : modular::SingleServiceApp<modular::Agent>,
                     modular::testing::QueuePersistenceAgentInterface {
 public:
  static void New() {
    new TestAgentApp;  // deleted in Stop().
  }

 private:
  using TestPoint = modular::testing::TestPoint;

  TestAgentApp() { modular::testing::Init(application_context(), __FILE__); }

  ~TestAgentApp() override {}

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

    services_.AddService<modular::testing::QueuePersistenceAgentInterface>(
        [this](fidl::InterfaceRequest<
               modular::testing::QueuePersistenceAgentInterface> request) {
          services_bindings_.AddBinding(this, std::move(request));
        });

    initialized_.Pass();
    callback();
  }

  // |Agent|
  void Connect(const fidl::String& requestor_url,
               fidl::InterfaceRequest<app::ServiceProvider> services) override {
    services_.AddBinding(std::move(services));
    modular::testing::GetStore()->Put("queue_persistence_test_agent_connected",
                                      "", [] {});
    msg_queue_->Receive([this](const fidl::String& message) {
      modular::testing::GetStore()->Put(
          "queue_persistence_test_agent_received_message", "", [] {});
    });
  }

  // |Agent|
  void RunTask(const fidl::String& task_id,
               const RunTaskCallback& callback) override {}

  // |Agent|
  void Stop(const StopCallback& callback) override {
    modular::testing::GetStore()->Put(
        "queue_persistence_test_agent_stopped", "", [this, callback] {
          TEST_PASS("Queue persistence test agent exited");

          auto binding =
              PassBinding();  // To invoke callback() after delete this.
          delete this;
          modular::testing::Done();
          callback();
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
        });
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

  modular::ServiceProviderImpl services_;
  fidl::BindingSet<modular::testing::QueuePersistenceAgentInterface>
      services_bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  TestAgentApp::New();
  loop.Run();
  return 0;
}
