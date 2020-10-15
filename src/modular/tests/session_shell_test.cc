// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/modular/testing/cpp/fake_agent.h>

#include <gmock/gmock.h>
#include <src/modular/lib/modular_config/modular_config_constants.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

#define TEST_NAME(SUFFIX) \
  std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" #SUFFIX;

// TODO(fxbug.dev/16363): Use modular_testing::AddModToStory() throughout the test.
using fuchsia::modular::AddMod;
using fuchsia::modular::StoryCommand;
using fuchsia::modular::StoryInfo2;
using fuchsia::modular::StoryState;
using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::ViewIdentifier;
using testing::ByRef;
using testing::Pointee;
using testing::UnorderedElementsAre;

constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";

namespace {

class SessionShellTest : public modular_testing::TestHarnessFixture {
 protected:
  SessionShellTest()
      : fake_session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()) {}
  // Shared boilerplate for configuring the test harness to intercept the
  // session shell, setting up the session shell mock object, running the test
  // harness, and waiting for the session shell to be successfully intercepted.
  // Note that this method blocks the thread until the session shell has started
  // up.
  //
  // Not done in SetUp() or the constructor to let the test reader know that
  // this is happening. Also, certain tests may want to change this flow.
  void RunHarnessAndInterceptSessionShell() {
    modular_testing::TestHarnessBuilder builder;
    builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([&] { return fake_session_shell_->is_running(); });
  }

  void RunHarnessAndInterceptSessionShellAndFakeModule(const std::string& story_name) {
    modular_testing::TestHarnessBuilder builder;
    builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    // Listen for the module we're going to create.
    auto test_module = modular_testing::FakeModule::CreateWithDefaultOptions();
    builder.InterceptComponent(test_module->BuildInterceptOptions());

    // Start the session shell
    builder.BuildAndRun(test_harness());

    // Create a new story using PuppetMaster and start a new story shell.
    fuchsia::modular::testing::ModularService svc;
    fuchsia::modular::PuppetMasterPtr puppet_master;
    svc.set_puppet_master(puppet_master.NewRequest());
    test_harness()->ConnectToModularService(std::move(svc));

    fuchsia::modular::StoryPuppetMasterPtr story_master;
    puppet_master->ControlStory(story_name, story_master.NewRequest());

    // Add at least one module to the story
    fuchsia::modular::Intent intent;
    intent.handler = test_module->url();
    intent.action = "action";

    modular_testing::AddModToStory(test_harness(), story_name, "modname", std::move(intent));

    // Wait for the session shell and test module
    RunLoopUntil([&] { return test_module->is_running(); });
  }

  std::unique_ptr<modular_testing::FakeSessionShell> fake_session_shell_;
};

class TestComponent : public modular_testing::FakeComponent {
 public:
  // |on_created| is called when the component is launched. |on_destroyed| is called when the
  // component is terminated.
  TestComponent(fit::function<void()> on_created, fit::function<void()> on_destroyed)
      : FakeComponent({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                       .sandbox_services = {"fuchsia.modular.SessionShellContext"}}),
        on_created_(std::move(on_created)),
        on_destroyed_(std::move(on_destroyed)) {}

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override { on_created_(); }

  void OnDestroy() override { on_destroyed_(); }

  fit::function<void()> on_created_;
  fit::function<void()> on_destroyed_;
};

TEST_F(SessionShellTest, RestartShell) {
  modular_testing::TestHarnessBuilder builder;

  // Overriding OnDestroy() and OnCreate() to ensure that there isn't a race condition when
  // verifying that the session_shell restarts properly.
  bool stopped = false;
  bool started = false;

  TestComponent session_shell([&] { started = true; }, [&] { stopped = true; });
  builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  EXPECT_FALSE(session_shell.is_running());
  RunLoopUntil([&] { return session_shell.is_running(); });

  started = false;

  fuchsia::modular::SessionShellContextPtr session_shell_context;
  session_shell.component_context()->svc()->Connect(session_shell_context.NewRequest());
  session_shell_context->Restart();

  RunLoopUntil([&] { return stopped; });

  // Tests that the session shell is restarted after a call to SessionShellContext::Restart.
  RunLoopUntil([&] { return started; });
}

TEST_F(SessionShellTest, GetStoryInfoNonexistentStory) {
  RunHarnessAndInterceptSessionShell();

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  bool tried_get_story_info = false;
  story_provider->GetStoryInfo2("X",
                                [&tried_get_story_info](fuchsia::modular::StoryInfo2 story_info) {
                                  EXPECT_TRUE(story_info.IsEmpty());
                                  tried_get_story_info = true;
                                });

  RunLoopUntil([&] { return tried_get_story_info; });
}

TEST_F(SessionShellTest, GetStoriesEmpty) {
  RunHarnessAndInterceptSessionShell();

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  bool called_get_stories = false;
  story_provider->GetStories2(
      nullptr, [&called_get_stories](const std::vector<fuchsia::modular::StoryInfo2>& stories) {
        EXPECT_THAT(stories, testing::IsEmpty());
        called_get_stories = true;
      });

  RunLoopUntil([&] { return called_get_stories; });
}

TEST_F(SessionShellTest, StartAndStopStoryWithExtraInfoMod) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell,
  // including a mod with extra info.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";

  // Have the mock session_shell record the sequence of story states it sees,
  // and confirm that it only sees the correct story id.
  std::vector<StoryState> sequence_of_story_states;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2([&sequence_of_story_states, kStoryId](StoryInfo2 story_info,
                                                                StoryState story_state,
                                                                StoryVisibilityState _) {
    ASSERT_TRUE(story_info.has_id());
    EXPECT_EQ(story_info.id(), kStoryId);
    sequence_of_story_states.push_back(story_state);
  });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);
  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  bool execute_called = false;
  story_master->Execute(
      [&execute_called](fuchsia::modular::ExecuteResult result) { execute_called = true; });
  RunLoopUntil([&] { return execute_called; });

  // Stop the story. Check that the story went through the correct sequence
  // of states (see StoryState FIDL file for valid state transitions). Since we
  // started it, ran it, and stopped it, the sequence is STOPPED -> RUNNING ->
  // STOPPING -> STOPPED.
  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(kStoryId, story_controller.NewRequest());
  bool stop_called = false;
  story_controller->Stop([&stop_called] { stop_called = true; });
  RunLoopUntil([&] { return stop_called; });
  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 4; });
  EXPECT_THAT(sequence_of_story_states,
              testing::ElementsAre(StoryState::STOPPED, StoryState::RUNNING, StoryState::STOPPING,
                                   StoryState::STOPPED));
}

TEST_F(SessionShellTest, StoryInfoBeforeAndAfterDelete) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";
  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));

  bool execute_and_get_story_info_called = false;
  story_master->Execute([&execute_and_get_story_info_called, kStoryId,
                         story_provider](fuchsia::modular::ExecuteResult result) {
    // Verify that the newly created story returns something for
    // GetStoryInfo().
    story_provider->GetStoryInfo2(kStoryId, [&execute_and_get_story_info_called,
                                             kStoryId](fuchsia::modular::StoryInfo2 story_info) {
      ASSERT_TRUE(story_info.has_id());
      EXPECT_EQ(story_info.id(), kStoryId);
      execute_and_get_story_info_called = true;
    });
  });
  RunLoopUntil([&] { return execute_and_get_story_info_called; });

  // Delete the story and confirm that the story info is null now.
  bool delete_called = false;
  puppet_master->DeleteStory(kStoryId, [&delete_called, kStoryId, story_provider] {
    story_provider->GetStoryInfo2(kStoryId, [](fuchsia::modular::StoryInfo2 story_info) {
      EXPECT_TRUE(story_info.IsEmpty());
    });
    delete_called = true;
  });
  RunLoopUntil([&] { return delete_called; });
}

TEST_F(SessionShellTest, DISABLED_AttachesAndDetachesView) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and start a new story shell.
  // Confirm that AttachView() is called.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  const char kStoryId[] = "my_story";
  // Have the mock session_shell record the sequence of story states it sees,
  // and confirm that it only sees the correct story id.
  std::vector<StoryState> sequence_of_story_states;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2([&sequence_of_story_states, kStoryId](StoryInfo2 story_info,
                                                                StoryState story_state,
                                                                StoryVisibilityState _) {
    EXPECT_TRUE(story_info.has_id());
    EXPECT_EQ(story_info.id(), kStoryId);
    sequence_of_story_states.push_back(story_state);
  });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);
  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  story_master->Execute([](fuchsia::modular::ExecuteResult result) {});

  bool called_attach_view = false;
  fake_session_shell_->set_on_attach_view(
      [&called_attach_view](ViewIdentifier) { called_attach_view = true; });

  RunLoopUntil([&] { return called_attach_view; });

  // Stop the story. Confirm that:
  //  a. DetachView() was called.
  //  b. The story went through the correct sequence
  // of states (see StoryState FIDL file for valid state transitions). Since we
  // started it, ran it, and stopped it, the sequence is STOPPED -> RUNNING ->
  // STOPPING -> STOPPED.
  bool called_detach_view = false;
  fake_session_shell_->set_on_detach_view(
      [&called_detach_view](ViewIdentifier) { called_detach_view = true; });
  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(kStoryId, story_controller.NewRequest());
  bool stop_called = false;
  story_controller->Stop([&stop_called] { stop_called = true; });
  RunLoopUntil([&] { return stop_called; });
  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 4; });
  EXPECT_TRUE(called_detach_view);
  EXPECT_THAT(sequence_of_story_states,
              testing::ElementsAre(StoryState::STOPPED, StoryState::RUNNING, StoryState::STOPPING,
                                   StoryState::STOPPED));
}

TEST_F(SessionShellTest, DISABLED_StoryStopDoesntWaitOnDetachView) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and start a new story shell.
  // Confirm that AttachView() is called.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";

  // Have the mock session_shell record the sequence of story states it sees,
  // and confirm that it only sees the correct story id.
  std::vector<StoryState> sequence_of_story_states;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2([&sequence_of_story_states, kStoryId](StoryInfo2 story_info,
                                                                StoryState story_state,
                                                                StoryVisibilityState _) {
    EXPECT_TRUE(story_info.has_id());
    EXPECT_EQ(story_info.id(), kStoryId);
    sequence_of_story_states.push_back(story_state);
  });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);

  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  story_master->Execute([](fuchsia::modular::ExecuteResult result) {});

  bool called_attach_view = false;
  fake_session_shell_->set_on_attach_view(
      [&called_attach_view](ViewIdentifier) { called_attach_view = true; });

  RunLoopUntil([&] { return called_attach_view; });

  // Stop the story. Confirm that:
  //  a. The story stopped, even though it didn't see the DetachView() response
  //   (it was artificially delayed for 1hr).
  //  b. The story went through the correct sequence of states (see StoryState
  //   FIDL file for valid state transitions). Since we started it, ran it, and
  //   stopped it, the sequence is STOPPED -> RUNNING -> STOPPING -> STOPPED.
  fake_session_shell_->set_detach_delay(zx::sec(60 * 60));
  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(kStoryId, story_controller.NewRequest());
  bool stop_called = false;
  story_controller->Stop([&stop_called] { stop_called = true; });

  RunLoopUntil([&] { return stop_called; });
  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 4; });
  EXPECT_THAT(sequence_of_story_states,
              testing::ElementsAre(StoryState::STOPPED, StoryState::RUNNING, StoryState::STOPPING,
                                   StoryState::STOPPED));
}

TEST_F(SessionShellTest, GetStoryInfo2HasId) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";
  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));

  bool execute_and_get_story_info_called = false;
  story_master->Execute([&execute_and_get_story_info_called, kStoryId,
                         story_provider](fuchsia::modular::ExecuteResult result) {
    // Verify that the newly created story returns something for
    // GetStoryInfo2().
    story_provider->GetStoryInfo2(kStoryId, [&execute_and_get_story_info_called,
                                             kStoryId](fuchsia::modular::StoryInfo2 story_info) {
      ASSERT_FALSE(story_info.IsEmpty());
      EXPECT_TRUE(story_info.has_id());
      EXPECT_EQ(story_info.id(), kStoryId);
      execute_and_get_story_info_called = true;
    });
  });
  RunLoopUntil([&] { return execute_and_get_story_info_called; });
}

TEST_F(SessionShellTest, GetStories2ReturnsStoryInfo) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";
  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));

  bool execute_and_get_stories_called = false;
  story_master->Execute([&execute_and_get_stories_called, kStoryId,
                         story_provider](fuchsia::modular::ExecuteResult result) {
    // Verify that GetStories2 returns the StoryInfo2 for the newly created story
    story_provider->GetStories2(/*watcher=*/nullptr,
                                [&execute_and_get_stories_called,
                                 kStoryId](std::vector<fuchsia::modular::StoryInfo2> story_infos) {
                                  ASSERT_FALSE(story_infos.empty());
                                  const auto& story_info = story_infos.at(0);
                                  ASSERT_FALSE(story_info.IsEmpty());
                                  EXPECT_TRUE(story_info.has_id());
                                  EXPECT_EQ(story_info.id(), kStoryId);
                                  execute_and_get_stories_called = true;
                                });
  });
  RunLoopUntil([&] { return execute_and_get_stories_called; });
}

TEST_F(SessionShellTest, StoryProviderWatcher) {
  static constexpr auto kStoryId = "my_story";

  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and start a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_NE(nullptr, story_provider);

  // Once the story is created, OnChange2 should be called with a StoryInfo2 that has the story ID.
  modular_testing::SimpleStoryProviderWatcher watcher;
  std::vector<StoryInfo2> on_change_calls;
  watcher.set_on_change_2(
      [&](StoryInfo2 story_info, StoryState /* unused */, StoryVisibilityState /* unused */) {
        on_change_calls.push_back(std::move(story_info));
      });
  std::vector<std::string> on_delete_calls;
  watcher.set_on_delete([&](const std::string& story_id) { on_delete_calls.push_back(story_id); });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);

  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  story_master->Execute([](const fuchsia::modular::ExecuteResult& result) {
    ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  });

  RunLoopUntil([&] { return !on_change_calls.empty(); });
  EXPECT_TRUE(on_change_calls.at(0).has_id());
  EXPECT_EQ(kStoryId, on_change_calls.at(0).id());

  // Delete the story twice. Expect that we are notified only once.
  EXPECT_TRUE(on_delete_calls.empty());
  int delete_story_count = 0;
  puppet_master->DeleteStory(kStoryId, [&delete_story_count] { delete_story_count++; });
  puppet_master->DeleteStory(kStoryId, [&delete_story_count] { delete_story_count++; });
  RunLoopUntil([&] { return delete_story_count == 2; });
  EXPECT_EQ(2, delete_story_count);

  // In order to ensure that both DeleteStory() operations have completed,
  // perform another operation that is enqueued after them and wait for it
  // to return.
  bool get_stories_done = false;
  story_provider->GetStories2(nullptr, [&](auto /*ignored*/) { get_stories_done = true; });
  RunLoopUntil([&] { return get_stories_done; });

  RunLoopUntil([&] { return !on_delete_calls.empty(); });
  EXPECT_EQ(1u, on_delete_calls.size());
}

TEST_F(SessionShellTest, StoryControllerAnnotate) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(story_name, story_controller.NewRequest());

  // Create some annotations, one for each variant of AnnotationValue.
  auto text_annotation_value = fuchsia::modular::AnnotationValue{};
  text_annotation_value.set_text("text_value");
  auto text_annotation = fuchsia::modular::Annotation{
      .key = "text_key", .value = fidl::MakeOptional(fidl::Clone(text_annotation_value))};

  auto bytes_annotation_value = fuchsia::modular::AnnotationValue{};
  bytes_annotation_value.set_bytes({0x01, 0x02, 0x03, 0x04});
  auto bytes_annotation = fuchsia::modular::Annotation{
      .key = "bytes_key", .value = fidl::MakeOptional(fidl::Clone(bytes_annotation_value))};

  fuchsia::mem::Buffer buffer{};
  std::string buffer_value = "buffer_value";
  ASSERT_TRUE(fsl::VmoFromString(buffer_value, &buffer));
  auto buffer_annotation_value = fuchsia::modular::AnnotationValue{};
  buffer_annotation_value.set_buffer(std::move(buffer));
  auto buffer_annotation = fuchsia::modular::Annotation{
      .key = "buffer_key", .value = fidl::MakeOptional(fidl::Clone(buffer_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(text_annotation));
  annotations.push_back(fidl::Clone(bytes_annotation));
  annotations.push_back(fidl::Clone(buffer_annotation));

  // Annotate the story.
  bool done_annotating{false};
  story_controller->Annotate(std::move(annotations),
                             [&](fuchsia::modular::StoryController_Annotate_Result result) {
                               EXPECT_FALSE(result.is_err());
                               done_annotating = true;
                             });
  RunLoopUntil([&] { return done_annotating; });

  // GetStoryInfo should contain the annotations.
  fuchsia::modular::StoryInfo2 story_info;
  bool done_getting_story_info = false;

  story_provider->GetStoryInfo2(story_name, [&](fuchsia::modular::StoryInfo2 data) {
    done_getting_story_info = true;
    story_info = std::move(data);
  });
  RunLoopUntil([&] { return done_getting_story_info; });

  EXPECT_FALSE(story_info.IsEmpty());
  EXPECT_TRUE(story_info.has_annotations());

  EXPECT_EQ(3u, story_info.annotations().size());

  EXPECT_THAT(
      story_info.mutable_annotations(),
      Pointee(UnorderedElementsAre(modular::annotations::AnnotationEq(ByRef(text_annotation)),
                                   modular::annotations::AnnotationEq(ByRef(bytes_annotation)),
                                   modular::annotations::AnnotationEq(ByRef(buffer_annotation)))));
}

// Verifies that Annotate merges new annotations, preserving existing ones.
TEST_F(SessionShellTest, StoryControllerAnnotateMerge) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(story_name, story_controller.NewRequest());

  // Create the initial set of annotations.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key", .value = fidl::MakeOptional(fidl::Clone(first_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(first_annotation));

  // Annotate the story.
  bool done{false};
  story_controller->Annotate(std::move(annotations),
                             [&](fuchsia::modular::StoryController_Annotate_Result result) {
                               EXPECT_FALSE(result.is_err());
                               done = true;
                             });
  RunLoopUntil([&] { return done; });

  // GetStoryData should contain the first annotation.
  done = false;
  story_provider->GetStoryInfo2(story_name, [&](fuchsia::modular::StoryInfo2 story_info) {
    EXPECT_FALSE(story_info.IsEmpty());
    EXPECT_TRUE(story_info.has_annotations());

    EXPECT_EQ(1u, story_info.annotations().size());

    EXPECT_EQ(story_info.annotations().at(0).key, first_annotation.key);
    EXPECT_EQ(story_info.annotations().at(0).value->text(), first_annotation_value.text());

    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Create another set of annotations that should be merged into the initial one.
  auto second_annotation_value = fuchsia::modular::AnnotationValue{};
  second_annotation_value.set_text("second_value");
  auto second_annotation = fuchsia::modular::Annotation{
      .key = "second_key", .value = fidl::MakeOptional(fidl::Clone(second_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations_2;
  annotations_2.push_back(fidl::Clone(second_annotation));

  // Annotate the story with the second set of annotations.
  done = false;
  story_controller->Annotate(std::move(annotations_2),
                             [&](fuchsia::modular::StoryController_Annotate_Result result) {
                               EXPECT_FALSE(result.is_err());
                               done = true;
                             });
  RunLoopUntil([&] { return done; });

  // GetStoryData should now return annotations from both the first and second set.
  done = false;
  story_provider->GetStoryInfo2(story_name, [&](fuchsia::modular::StoryInfo2 story_info) {
    EXPECT_FALSE(story_info.IsEmpty());

    EXPECT_EQ(2u, story_info.annotations().size());

    EXPECT_THAT(story_info.mutable_annotations(),
                Pointee(UnorderedElementsAre(
                    modular::annotations::AnnotationEq(ByRef(first_annotation)),
                    modular::annotations::AnnotationEq(ByRef(second_annotation)))));

    done = true;
  });
  RunLoopUntil([&] { return done; });
}

// Verifies that StoryController.Annotate returns an error when one of the annotations
// has a buffer value that exceeds MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES.
TEST_F(SessionShellTest, StoryControllerAnnotateBufferValueTooBig) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(story_name, story_controller.NewRequest());

  // Create an annotation with a large buffer value.
  fuchsia::mem::Buffer buffer{};
  std::string buffer_value(fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES + 1, 'x');
  ASSERT_TRUE(fsl::VmoFromString(buffer_value, &buffer));

  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_buffer(std::move(buffer));
  auto annotation = fuchsia::modular::Annotation{
      .key = "buffer_key", .value = fidl::MakeOptional(std::move(annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  // Annotate the story.
  bool done{false};
  story_controller->Annotate(
      std::move(annotations), [&](fuchsia::modular::StoryController_Annotate_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::modular::AnnotationError::VALUE_TOO_BIG, result.err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that StoryPuppetMaster.Annotate returns an error when one of the annotations
// has a buffer value that exceeds MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES.
TEST_F(SessionShellTest, StoryPuppetMasterAnnotateBufferValueTooBig) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  // Connect to StoryPuppetMaster.
  fuchsia::modular::testing::ModularService svc;
  fuchsia::modular::PuppetMasterPtr puppet_master;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  puppet_master->ControlStory(story_name, story_puppet_master.NewRequest());

  // Create an annotation with a large buffer value.
  fuchsia::mem::Buffer buffer{};
  std::string buffer_value(fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES + 1, 'x');
  ASSERT_TRUE(fsl::VmoFromString(buffer_value, &buffer));

  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_buffer(std::move(buffer));
  auto annotation = fuchsia::modular::Annotation{
      .key = "buffer_key", .value = fidl::MakeOptional(std::move(annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  // Annotate the story.
  bool done{false};
  story_puppet_master->Annotate(
      std::move(annotations), [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::modular::AnnotationError::VALUE_TOO_BIG, result.err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that Annotate returns an error when adding new annotations to exceeds
// MAX_ANNOTATIONS_PER_STORY.
TEST_F(SessionShellTest, StoryControllerAnnotateTooMany) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(story_name, story_controller.NewRequest());

  // A single Annotate call should not accept more annotations than allowed on a single story.
  ASSERT_GE(fuchsia::modular::MAX_ANNOTATIONS_PER_STORY,
            fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE);

  // Annotate the story repeatedly, in batches of MAX_ANNOTATIONS_PER_UPDATE items, in order
  // to reach, but not exceed the MAX_ANNOTATIONS_PER_STORY limit.
  for (unsigned int num_annotate_calls = 0;
       num_annotate_calls <
       fuchsia::modular::MAX_ANNOTATIONS_PER_STORY / fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE;
       ++num_annotate_calls) {
    std::vector<fuchsia::modular::Annotation> annotations;

    // Create MAX_ANNOTATIONS_PER_UPDATE annotations for each call to Annotate.
    for (unsigned int num_annotations = 0;
         num_annotations < fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE; ++num_annotations) {
      auto annotation_value = fuchsia::modular::AnnotationValue{};
      annotation_value.set_text("test_annotation_value");
      auto annotation =
          fuchsia::modular::Annotation{.key = "annotation_" + std::to_string(num_annotate_calls) +
                                              "_" + std::to_string(num_annotations),
                                       .value = fidl::MakeOptional(std::move(annotation_value))};
      annotations.push_back(std::move(annotation));
    }

    // Annotate the story.
    bool done{false};
    story_controller->Annotate(
        std::move(annotations), [&](fuchsia::modular::StoryController_Annotate_Result result) {
          EXPECT_FALSE(result.is_err())
              << "Annotate call #" << num_annotate_calls << " returned an error when trying to add "
              << std::to_string(fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE)
              << " annotations to the story.";
          done = true;
        });
    RunLoopUntil([&] { return done; });
  }

  // Create some more annotations for a total of (MAX_ANNOTATIONS_PER_STORY + 1) on the story.
  std::vector<fuchsia::modular::Annotation> annotations;

  for (unsigned int num_annotations = 0;
       num_annotations < (fuchsia::modular::MAX_ANNOTATIONS_PER_STORY %
                          fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE) +
                             1;
       ++num_annotations) {
    auto annotation_value = fuchsia::modular::AnnotationValue{};
    annotation_value.set_text("test_annotation_value");
    auto annotation =
        fuchsia::modular::Annotation{.key = "excess_annotation_" + std::to_string(num_annotations),
                                     .value = fidl::MakeOptional(std::move(annotation_value))};
    annotations.push_back(std::move(annotation));
  }

  // Annotate the story.
  bool done{false};
  story_controller->Annotate(
      std::move(annotations), [&](fuchsia::modular::StoryController_Annotate_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS, result.err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that a call to StoryController.Annotate results in a StoryProviderWatcher.OnChange2
// being called with the updated annotations.
TEST_F(SessionShellTest, StoryControllerAnnotateNotifiesWatcher) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  fuchsia::modular::StoryControllerPtr story_controller;
  story_provider->GetController(story_name, story_controller.NewRequest());

  // Watch the story for new annotations.
  auto num_on_change_2_calls = 0;
  auto num_annotations = 0;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2(
      [&num_on_change_2_calls, &num_annotations](StoryInfo2 story_info, StoryState /*unused*/,
                                                 StoryVisibilityState /*unused*/) {
        num_on_change_2_calls++;
        num_annotations = story_info.mutable_annotations()->size();
      });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);

  // Create a set of annotations, containing a single annotation.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key", .value = fidl::MakeOptional(fidl::Clone(first_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(first_annotation));

  // Annotate the story.
  bool done{false};
  story_controller->Annotate(std::move(annotations),
                             [&](fuchsia::modular::StoryController_Annotate_Result result) {
                               EXPECT_FALSE(result.is_err());
                               done = true;
                             });
  RunLoopUntil([&] { return done; });

  RunLoopUntil([&] { return num_on_change_2_calls > 0; });
  EXPECT_EQ(1, num_annotations);
}

// Verifies that a call to StoryPuppetMaster.Annotate results in a StoryProviderWatcher.OnChange2
// being called with the updated annotations.
TEST_F(SessionShellTest, StoryPuppetMasterAnnotateNotifiesWatcher) {
  const auto story_name = TEST_NAME(story);

  RunHarnessAndInterceptSessionShellAndFakeModule(story_name);

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();

  // Connect to StoryPuppetMaster.
  fuchsia::modular::testing::ModularService svc;
  fuchsia::modular::PuppetMasterPtr puppet_master;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  puppet_master->ControlStory(story_name, story_puppet_master.NewRequest());

  // Watch the story for new annotations.
  auto num_on_change_2_calls = 0;
  auto num_annotations = 0;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2(
      [&num_on_change_2_calls, &num_annotations](StoryInfo2 story_info, StoryState /*unused*/,
                                                 StoryVisibilityState /*unused*/) {
        num_on_change_2_calls++;
        num_annotations = story_info.mutable_annotations()->size();
      });
  watcher.Watch(story_provider, /*on_get_stories=*/nullptr);

  // Create a set of annotations, containing a single annotation.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key", .value = fidl::MakeOptional(fidl::Clone(first_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(first_annotation));

  // Annotate the story.
  bool done{false};
  story_puppet_master->Annotate(std::move(annotations),
                                [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                                  EXPECT_FALSE(result.is_err());
                                  done = true;
                                });
  RunLoopUntil([&] { return done; });

  RunLoopUntil([&] { return num_on_change_2_calls > 0; });
  EXPECT_EQ(1, num_annotations);
}

class ServicePublishingSessionShell : public modular_testing::FakeSessionShell {
 public:
  explicit ServicePublishingSessionShell(modular_testing::FakeComponent::Args args)
      : FakeSessionShell(std::move(args)) {}

  static std::unique_ptr<ServicePublishingSessionShell> CreateWithDefaultOptions() {
    return std::make_unique<ServicePublishingSessionShell>(modular_testing::FakeComponent::Args{
        .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
        .sandbox_services = FakeSessionShell::GetDefaultSandboxServices()});
  }

  int fake_service_connect_count() const { return fake_service_connect_count_; }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    FakeSessionShell::OnCreate(std::move(startup_info));
    component_context()->outgoing()->AddPublicService(
        fit::function<void(fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol>)>(
            [this](fidl::InterfaceRequest<fuchsia::testing::modular::TestProtocol> request) {
              ++fake_service_connect_count_;
              service_channels_.push_back(request.TakeChannel());
            }));
  }

  std::vector<zx::channel> service_channels_;
  int fake_service_connect_count_ = 0;
};

// Show that the session shell can publish a service that is accessible to agents
// via the agent_service_index.
TEST_F(SessionShellTest, SessionShellCanPublishServicesToAgents) {
  auto session_shell = ServicePublishingSessionShell::CreateWithDefaultOptions();
  auto agent = modular_testing::FakeAgent::CreateWithDefaultOptions();

  fuchsia::modular::testing::TestHarnessSpec spec;
  std::vector<fuchsia::modular::session::AgentServiceIndexEntry> agent_service_index;
  fuchsia::modular::session::AgentServiceIndexEntry agent_service;
  agent_service.set_service_name(fuchsia::testing::modular::TestProtocol::Name_);
  agent_service.set_agent_url(session_shell->url());
  agent_service_index.emplace_back(std::move(agent_service));

  spec.mutable_sessionmgr_config()->set_agent_service_index(std::move(agent_service_index));
  spec.mutable_sessionmgr_config()->set_session_agents({agent->url()});

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  auto agent_intercept_options = agent->BuildInterceptOptions();
  agent_intercept_options.sandbox_services.push_back(
      fuchsia::testing::modular::TestProtocol::Name_);
  builder.InterceptComponent(std::move(agent_intercept_options));
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return session_shell->is_running() && agent->is_running(); });

  auto service_ptr =
      agent->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>();
  service_ptr.set_error_handler(
      [](zx_status_t status) { FX_PLOGS(FATAL, status) << "channel should not have closed"; });

  RunLoopUntil([&] { return session_shell->fake_service_connect_count() > 0; });
  EXPECT_EQ(1, session_shell->fake_service_connect_count());
  service_ptr.set_error_handler(nullptr);
}

}  // namespace
