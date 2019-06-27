// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/message_queue/cpp/message_sender_client.h>
#include <lib/modular_test_harness/cpp/fake_agent.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <src/lib/fxl/logging.h>

namespace {

class TriggerTest : public modular::testing::TestHarnessFixture {
 protected:
  void SetUp() override {
    // Intercept |fake_module_|
    fake_module_ = std::make_unique<modular::testing::FakeModule>();
    fake_module_url_ = modular::testing::GenerateFakeUrl();
    builder_.InterceptComponent(
        fake_module_->GetOnCreateHandler(),
        {.url = fake_module_url_,
         .sandbox_services = modular::testing::FakeModule::GetSandboxServices()});

    // Intercept |fake_agent_|
    fake_agent_ = std::make_unique<modular::testing::FakeAgent>();
    fake_agent_url_ = modular::testing::GenerateFakeUrl();
    builder_.InterceptComponent(
        fake_agent_->GetOnCreateHandler(),
        {.url = fake_agent_url_,
         .sandbox_services = modular::testing::FakeAgent::GetSandboxServices()});

    builder_.BuildAndRun(test_harness());

    // Start a mod in a story.
    modular::testing::AddModToStory(test_harness(), "story_name", "mod_name",
                                    fuchsia::modular::Intent{.handler = fake_module_url_});
    RunLoopUntil([&] { return fake_module_->is_running(); });

    // Start an agent.
    fuchsia::sys::ServiceProviderPtr agent_services;
    fake_module_->modular_component_context()->ConnectToAgent(
        fake_agent_url_, agent_services.NewRequest(), agent_controller_.NewRequest());
    RunLoopUntil([&] { return fake_agent_->is_running(); });
  }

  modular::testing::TestHarnessBuilder builder_;
  std::unique_ptr<modular::testing::FakeModule> fake_module_;
  std::unique_ptr<modular::testing::FakeAgent> fake_agent_;
  std::string fake_module_url_;
  std::string fake_agent_url_;
  fuchsia::modular::AgentControllerPtr agent_controller_;
};

// Tests that an agent is woken up on a new message.
TEST_F(TriggerTest, AgentWakesUpOnNewMessage) {
  // Create a message queue and schedule a task to be run on receiving a
  // message on it.
  fuchsia::modular::MessageQueuePtr msg_queue;
  fake_agent_->modular_component_context()->ObtainMessageQueue("Trigger Queue",
                                                               msg_queue.NewRequest());
  fuchsia::modular::TaskInfo task_info;
  task_info.task_id = "message_queue_message";
  task_info.trigger_condition.set_message_on_queue("Trigger Queue");
  task_info.persistent = true;

  bool schedule_task_complete = false;
  fake_agent_->agent_context()->ScheduleTaskWithCompletion(
      std::move(task_info), [&](bool finished) { schedule_task_complete = finished; });

  // Wait for the schedule task to complete.
  RunLoopUntil([&] { return schedule_task_complete; });

  // Set the callback for when the framework tells the agent to trigger a
  // task.
  bool agent_received_message = false;
  fake_agent_->set_on_run_task(
      [&](std::string task_id, fuchsia::modular::Agent::RunTaskCallback callback) {
        if (task_id == "message_queue_message") {
          agent_received_message = true;
        }
        callback();
      });

  // Stop the agent.
  agent_controller_.Unbind();
  RunLoopUntil([&] { return !fake_agent_->is_running(); });

  // Send a message from the module to the stopped agent which should
  // trigger it to start.
  modular::MessageSenderClient message_sender;
  msg_queue->GetToken([&](std::string token) {
    fake_module_->modular_component_context()->GetMessageSender(token, message_sender.NewRequest());
    message_sender.Send("Time to wake up...");
  });

  RunLoopUntil([&] { return agent_received_message; });
}

// Tests that an agent is woken up on an explicitly deleted message queue.
TEST_F(TriggerTest, AgentWakesUpOnExplicitMessageQueueDelete) {
  // The message queue that is used to verify deletion triggers from explicit
  // deletes.
  fuchsia::modular::MessageQueuePtr explicit_msg_queue;
  fake_module_->modular_component_context()->ObtainMessageQueue("explicit_test",
                                                                explicit_msg_queue.NewRequest());

  // |schedule_task_complete| is used to ensure we register the deletion watcher
  // before calling delete on the message queue.
  bool schedule_task_complete = false;
  std::string explicit_msg_queue_token;
  explicit_msg_queue->GetToken([&](fidl::StringPtr token) {
    explicit_msg_queue_token = token;

    // Schedule a task to process a message queue deletion.
    fuchsia::modular::TaskInfo task_info;
    task_info.task_id = token;
    task_info.trigger_condition.set_queue_deleted(token);
    task_info.persistent = true;
    fake_agent_->agent_context()->ScheduleTaskWithCompletion(
        std::move(task_info), [&](bool finished) { schedule_task_complete = finished; });
  });

  // Wait for the schedule task to complete.
  RunLoopUntil([&] { return schedule_task_complete; });

  // Set the callback for when the framework tells the agent to trigger a
  // task.
  bool agent_proccessed_queue_deletion = false;
  fake_agent_->set_on_run_task(
      [&](std::string task_id, fuchsia::modular::Agent::RunTaskCallback callback) {
        if (task_id == explicit_msg_queue_token) {
          agent_proccessed_queue_deletion = true;
        }
        callback();
      });

  // Stop the agent.
  agent_controller_.Unbind();
  RunLoopUntil([&] { return !fake_agent_->is_running(); });

  fake_module_->modular_component_context()->DeleteMessageQueue("explicit_test");

  RunLoopUntil([&] { return agent_proccessed_queue_deletion; });
}

// Tests that an agent is woken up on an implicitely deleted message queue as
// part of a story tear down.
TEST_F(TriggerTest, AgentWakesUpOnImplicitMessageQueueDelete) {
  // The message queue that is used to verify deletion triggers from explicit
  // deletes.
  fuchsia::modular::MessageQueuePtr implicit_msg_queue;
  fake_module_->modular_component_context()->ObtainMessageQueue("implicit_test",
                                                                implicit_msg_queue.NewRequest());

  // |schedule_task_complete| is used to ensure we register the deletion watcher
  // before calling delete on the message queue.
  bool schedule_task_complete = false;

  std::string implicit_msg_queue_token;
  implicit_msg_queue->GetToken([&](std::string token) {
    implicit_msg_queue_token = token;

    // Schedule a task to process a message queue deletion.
    fuchsia::modular::TaskInfo task_info;
    task_info.task_id = token;
    task_info.trigger_condition.set_queue_deleted(token);
    task_info.persistent = true;
    fake_agent_->agent_context()->ScheduleTaskWithCompletion(
        std::move(task_info), [&](bool finished) { schedule_task_complete = finished; });
  });

  // Wait for the schedule task to complete.
  RunLoopUntil([&] { return schedule_task_complete; });

  // Set the callback for when the framework tells the agent to trigger a
  // task.
  bool agent_proccessed_queue_deletion = false;
  fake_agent_->set_on_run_task(
      [&](std::string task_id, fuchsia::modular::Agent::RunTaskCallback callback) {
        if (task_id == implicit_msg_queue_token) {
          agent_proccessed_queue_deletion = true;
        }
        callback();
      });

  // Stop the agent.
  agent_controller_.Unbind();
  RunLoopUntil([&] { return !fake_agent_->is_running(); });

  // Connect to PuppetMaster and ComponentContext.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  // Delete the story to trigger the deletion of the message queue that the
  // module created.
  puppet_master->DeleteStory("story_name", []() {});

  RunLoopUntil([&] { return agent_proccessed_queue_deletion; });
}

}  // namespace
