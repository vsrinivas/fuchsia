// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <test/peridot/tests/componentcontext/cpp/fidl.h>

#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/callback/scoped_callback.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/fidl/message_receiver_client.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/component_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::componentcontext::ComponentContextTestServicePtr;

namespace {

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

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Root module initialized"};
  TestPoint one_agent_connected_{"One agent accepted connection"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : steps_(
            kTotalSimultaneousTests,
            [this, module_host] { Signal(modular::testing::kTestShutdown); }),
        weak_ptr_factory_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);

    initialized_.Pass();

    // Exercise fuchsia::modular::ComponentContext.ConnectToAgent()
    module_host->module_context()->GetComponentContext(
        component_context_.NewRequest());

    fuchsia::sys::ServiceProviderPtr one_agent_services;
    component_context_->ConnectToAgent(kOneAgentUrl,
                                       one_agent_services.NewRequest(),
                                       one_agent_controller.NewRequest());
    ConnectToService(one_agent_services.get(),
                     one_agent_interface_.NewRequest());

    Await("one_agent_connected", [this] {
      one_agent_connected_.Pass();
      TestMessageQueue([this] {
        TestAgentController(callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                                 [this] { steps_.Step(); }));
      });
    });

    TestUnstoppableAgent(callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                              [this] { steps_.Step(); }));
  }

  TestPoint stopped_{"Root module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint msg_queue_communicated_{
      "Communicated message between fuchsia::modular::Agent one using a "
      "fuchsia::modular::MessageQueue"};

  // Tests message queues. Calls |done_cb| when completed successfully.
  void TestMessageQueue(std::function<void()> done_cb) {
    constexpr char kTestMessage[] = "test message!";

    component_context_->ObtainMessageQueue("root_msg_queue",
                                           msg_queue_.NewRequest());

    // MessageQueueManager shouldn't send us anything just yet.
    msg_receiver_ = std::make_unique<modular::MessageReceiverClient>(
        msg_queue_.get(),
        [this, done_cb, kTestMessage](const fidl::StringPtr& msg,
                                      std::function<void()> ack) {
          ack();
          // We only want one message.
          msg_receiver_.reset();

          if (msg == kTestMessage) {
            msg_queue_communicated_.Pass();
          }
          done_cb();
        });

    msg_queue_->GetToken([this, kTestMessage](const fidl::StringPtr& token) {
      one_agent_interface_->SendToMessageQueue(token, kTestMessage);
    });
  }

  TestPoint one_agent_stopped_{"One agent stopped"};

  // Tests fuchsia::modular::AgentController. Calls |done_cb| when completed
  // successfully.
  void TestAgentController(std::function<void()> done_cb) {
    // Closing the agent controller should trigger the agent to stop.
    one_agent_controller.Unbind();

    Await("one_agent_stopped", [this, done_cb] {
      one_agent_stopped_.Pass();
      done_cb();
    });
  }

  // Start an agent that will not stop of its own accord.
  void TestUnstoppableAgent(std::function<void()> done_cb) {
    fuchsia::sys::ServiceProviderPtr unstoppable_agent_services;
    component_context_->ConnectToAgent(
        kUnstoppableAgent, unstoppable_agent_services.NewRequest(),
        unstoppable_agent_controller_.NewRequest());

    // After 500ms close the fuchsia::modular::AgentController for the
    // unstoppable agent.
    // TODO(jimbe) We don't check if the agent started running in the allotted
    // time, so this test isn't reliable. We need to make a call to the agent
    // and wait for a response.
    async::PostDelayedTask(
        async_get_default(),
        callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                             [this, done_cb] {
                               unstoppable_agent_controller_.Unbind();
                               done_cb();
                             }),
        zx::msec(500));
  }

  CounterTrigger steps_;

  fuchsia::modular::AgentControllerPtr one_agent_controller;
  ComponentContextTestServicePtr one_agent_interface_;
  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::MessageQueuePtr msg_queue_;

  fuchsia::modular::AgentControllerPtr unstoppable_agent_controller_;

  std::unique_ptr<modular::MessageReceiverClient> msg_receiver_;

  fxl::WeakPtrFactory<TestApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
