// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "lib/app/cpp/connect.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/component/fidl/message_queue.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/module/fidl/module.fidl.h"
#include "peridot/lib/fidl/message_receiver_client.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/component_context/test_agent1_interface.fidl.h"

using modular::testing::TestPoint;

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr fxl::TimeDelta kTimeout = fxl::TimeDelta::FromSeconds(15);

constexpr char kTest1Agent[] =
    "file:///system/apps/modular_tests/component_context_test_agent1";

constexpr char kUnstoppableAgent[] =
    "file:///system/apps/modular_tests/component_context_unstoppable_agent";

constexpr int kTotalSimultaneousTests = 2;

// Execute a trigger after the counter reaches a particular value OR if the
// count is canceled.
class CounterTrigger {
 public:
  CounterTrigger(int count, std::function<void()> trigger)
      : count_(count), trigger_(std::move(trigger)) {}
  void Step() {
    if (!finished_) {
      // If this CHECK triggers, then you've called Step() more times than
      // you passed for |count| into the constructor.
      FXL_CHECK(count_ > 0);
      if (count_ && --count_ == 0) {
        Finished();
      }
    }
  }

  // It's safe to call Cancel() at any time, even if the trigger has already
  // executed.
  void Cancel() { Finished(); }

 private:
  void Finished() {
    if (!finished_) {
      finished_ = true;
      trigger_();
    }
  }

  int count_;
  const std::function<void()> trigger_;
  bool finished_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(CounterTrigger);
};

class ParentApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new ParentApp;  // deletes itself in Stop() or after timeout.
  }

 private:
  ParentApp()
      : steps_(kTotalSimultaneousTests, [this] { module_context_->Done(); }) {
    TestInit(__FILE__);
  }
  ~ParentApp() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    initialized_.Pass();

    // Exercise ComponentContext.ConnectToAgent()
    module_context_->GetComponentContext(component_context_.NewRequest());

    app::ServiceProviderPtr agent1_services;
    component_context_->ConnectToAgent(kTest1Agent,
                                       agent1_services.NewRequest(),
                                       agent1_controller.NewRequest());
    ConnectToService(agent1_services.get(), agent1_interface_.NewRequest());

    modular::testing::GetStore()->Get(
        "test_agent1_connected", [this](const fidl::String&) {
          agent1_connected_.Pass();
          TestMessageQueue([this] {
            TestAgentController(Protect([this] { steps_.Step(); }));
          });
        });

    TestUnstoppableAgent(Protect([this] { steps_.Step(); }));

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { steps_.Cancel(); }), kTimeout);
  }

  // Tests message queues. Calls |done_cb| when completed successfully.
  void TestMessageQueue(std::function<void()> done_cb) {
    constexpr char kTestMessage[] = "test message!";

    component_context_->ObtainMessageQueue("root_msg_queue",
                                           msg_queue_.NewRequest());

    // MessageQueueManager shouldn't send us anything just yet.
    msg_receiver_ = std::make_unique<modular::MessageReceiverClient>(
        msg_queue_.get(),
        [this, done_cb, kTestMessage](const fidl::String& msg,
                                      std::function<void()> ack) {
          ack();
          // We only want one message.
          msg_receiver_.reset();

          if (msg == kTestMessage) {
            msg_queue_communicated_.Pass();
          }
          done_cb();
        });

    msg_queue_->GetToken([this, kTestMessage](const fidl::String& token) {
      agent1_interface_->SendToMessageQueue(token, kTestMessage);
    });
  }

  // Tests AgentController. Calls |done_cb| when completed successfully.
  void TestAgentController(std::function<void()> done_cb) {
    // Closing the agent controller should trigger the agent to stop.
    agent1_controller.reset();

    modular::testing::GetStore()->Get("test_agent1_stopped",
                                      [this, done_cb](const fidl::String&) {
                                        agent1_stopped_.Pass();
                                        done_cb();
                                      });
  }

  // Start an agent that will not stop of its own accord.
  void TestUnstoppableAgent(std::function<void()> done_cb) {
    app::ServiceProviderPtr unstoppable_agent_services;
    component_context_->ConnectToAgent(
        kUnstoppableAgent, unstoppable_agent_services.NewRequest(),
        unstoppable_agent_controller_.NewRequest());

    // After 500ms close the AgentController for the unstoppable agent.
    // TODO(jimbe) We don't check if the agent started running in the allotted
    // time, so this test isn't reliable. We need to make a call to the agent
    // and wait for a response.
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this, done_cb] {
          unstoppable_agent_controller_.reset();
          done_cb();
        }),
        fxl::TimeDelta::FromMilliseconds(500));
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  CounterTrigger steps_;

  modular::ModuleContextPtr module_context_;
  modular::AgentControllerPtr agent1_controller;
  modular::testing::Agent1InterfacePtr agent1_interface_;
  modular::ComponentContextPtr component_context_;
  modular::MessageQueuePtr msg_queue_;

  modular::AgentControllerPtr unstoppable_agent_controller_;

  std::unique_ptr<modular::MessageReceiverClient> msg_receiver_;

  TestPoint initialized_{"Root module initialized"};
  TestPoint stopped_{"Root module stopped"};
  TestPoint agent1_connected_{"Agent1 accepted connection"};
  TestPoint agent1_stopped_{"Agent1 stopped"};
  TestPoint msg_queue_communicated_{
      "Communicated message between Agent1 using a MessageQueue"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
