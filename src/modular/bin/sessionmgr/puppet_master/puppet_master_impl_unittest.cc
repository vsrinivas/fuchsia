// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/testing/test_story_command_executor.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

#define TEST_NAME(SUFFIX) \
  std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" #SUFFIX;

namespace modular {
namespace {

using ::testing::ByRef;
using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

fuchsia::modular::Intent CreateEmptyIntent(const std::string& action,
                                           const std::string& handler = "") {
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  if (!handler.empty()) {
    intent.handler = "mod_url";
  }
  return intent;
}

fuchsia::modular::StoryCommand MakeAddModCommand(const std::string& mod_name) {
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name_transitional = mod_name;
  auto intent = CreateEmptyIntent("intent_action", "mod_url");
  intent.parameters.emplace();
  intent.Clone(&add_mod.intent);
  fuchsia::modular::StoryCommand command;
  command.set_add_mod(std::move(add_mod));
  return command;
}

fuchsia::modular::StoryCommand MakeRemoveModCommand(std::string mod_name) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name_transitional = mod_name;
  command.set_remove_mod(std::move(remove_mod));
  return command;
}

class PuppetMasterTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage();
    impl_ = std::make_unique<PuppetMasterImpl>(session_storage_.get(), &executor_);
    impl_->Connect(ptr_.NewRequest());
  }

  fuchsia::modular::StoryPuppetMasterPtr ControlStory(std::string story_name) {
    story_name_ = story_name;
    fuchsia::modular::StoryPuppetMasterPtr ptr;
    ptr_->ControlStory(story_name, ptr.NewRequest());
    return ptr;
  }

  void EnqueueAddModCommand(const fuchsia::modular::StoryPuppetMasterPtr& story,
                            const std::string& module_name) {
    FX_CHECK(story_name_.has_value());

    // Add the module.
    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(MakeAddModCommand(module_name));
    story->Enqueue(std::move(commands));

    // Instruct our test executor to return an OK status, and since we're going to
    // AddMod, give the executor a StoryStorage.
    executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
    executor_.SetStoryStorage(GetStoryStorage(session_storage_.get(), story_name_.value()));
  }

 protected:
  std::optional<std::string> story_name_;
  modular_testing::TestStoryCommandExecutor executor_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> impl_;
  fuchsia::modular::PuppetMasterPtr ptr_;
};

TEST_F(PuppetMasterTest, CommandsAreSentToExecutor) {
  // This should create a new story in StoryStorage called "foo".
  auto story = ControlStory("foo");

  // Enqueue some commands. Do this twice and show that all the commands show
  // up as one batch.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));
  commands.clear();  // restore from "unspecified state"
  commands.push_back(MakeRemoveModCommand("two"));
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));

  // Commands are not run until Execute() is called.
  RunLoopUntilIdle();
  EXPECT_EQ(0, executor_.execute_count());

  fuchsia::modular::ExecuteResult result;
  bool done{false};
  // Instruct our test executor to return an OK status.
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);

  EXPECT_EQ("foo", executor_.last_story_id());
  ASSERT_EQ(3u, executor_.last_commands().size());
  EXPECT_EQ("one", executor_.last_commands().at(0).remove_mod().mod_name_transitional);
  EXPECT_EQ("two", executor_.last_commands().at(1).remove_mod().mod_name_transitional);
  EXPECT_EQ("three", executor_.last_commands().at(2).remove_mod().mod_name_transitional);
}

TEST_F(PuppetMasterTest, CommandsAreSentToExecutor_IfWeCloseStoryChannel) {
  // We're going to call Execute(), and then immediately drop the
  // StoryPuppetMaster connection. We won't get a callback, but we still
  // expect that the commands are executed.
  auto story = ControlStory("foo");

  // Enqueue some commands. Do this twice and show that all the commands show
  // up as one batch.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  fuchsia::modular::ExecuteResult result;
  bool callback_called{false};
  // Instruct our test executor to return an OK status.
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  story->Execute([&](fuchsia::modular::ExecuteResult r) { callback_called = true; });
  story.Unbind();
  RunLoopUntil([&]() { return executor_.execute_count() > 0; });
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1, executor_.execute_count());
}

TEST_F(PuppetMasterTest, MultipleExecuteCalls) {
  // Create a new story, and then execute some new commands on the same
  // connection. We should see that the StoryCommandExecutor receives the story
  // id that it reported after successful creation of the story on the last
  // execution.
  auto story = ControlStory("foo");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  bool done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) { done = true; });
  RunLoopUntil([&]() { return done; });
  auto story_id = executor_.last_story_id();

  // Execute more commands.
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));
  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult r) { done = true; });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(story_id, executor_.last_story_id());
}

TEST_F(PuppetMasterTest, NewStoriesAreKeptSeparate) {
  // Creating two new stories at the same time is OK and they are kept
  // separate.
  auto story1 = ControlStory("story1");
  auto story2 = ControlStory("story2");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  bool done{false};
  story1->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  auto story1_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("one", executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  done = false;
  story2->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(2, executor_.execute_count());
  auto story2_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("two", executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  // The two IDs should be different, because we gave the two stories different
  // names.
  EXPECT_NE(story1_id, story2_id);
}

TEST_F(PuppetMasterTest, ControlExistingStory) {
  // Controlling the same story from two connections is OK. The first call to
  // Execute() will create the story, and the second will re-use the same story
  // record.
  auto story1 = ControlStory("foo");
  auto story2 = ControlStory("foo");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  bool done{false};
  story1->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  auto story_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("one", executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, std::nullopt);
  done = false;
  story2->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(2, executor_.execute_count());
  EXPECT_EQ(story_id, executor_.last_story_id());
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("two", executor_.last_commands().at(0).remove_mod().mod_name_transitional);
}

TEST_F(PuppetMasterTest, DeleteStory) {
  // Create a story.
  auto story_id = session_storage_->CreateStory("foo", /*annotations=*/{});

  // Delete it
  bool done{};
  ptr_->DeleteStory("foo", [&] { done = true; });
  RunLoopUntil([&] { return done; });

  EXPECT_EQ(session_storage_->GetStoryData(story_id), nullptr);
}

TEST_F(PuppetMasterTest, DeleteStoryWithQueuedCommands) {
  const char* kStoryName = "DeleteWithQueuedCommandsStory";
  const char* kModuleName = "DeleteWithQueuedCommandsModule";

  // Call PuppetMaster directly to create & control a new Story.
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
  impl_->ControlStory(kStoryName, story_puppet_master.NewRequest());

  // Push an AddMod command to the StoryPuppetMaster.
  bool is_story_puppet_master_closed = false;
  story_puppet_master.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);
    is_story_puppet_master_closed = true;
  });
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeAddModCommand(kModuleName));
  story_puppet_master->Enqueue(std::move(commands));
  story_puppet_master->Execute([](fuchsia::modular::ExecuteResult) {
    // Execute() should never be processed
    ADD_FAILURE();
  });

  // Call PuppetMaster directly (i.e. without requiring the loop to be spun)
  // to delete the Story before the commands can be executed.
  impl_->DeleteStory(kStoryName, []() {});

  // Spin the loop and expect that the StoryPuppetMaster be disconnected.
  RunLoopUntilIdle();
  EXPECT_TRUE(is_story_puppet_master_closed);
}

TEST_F(PuppetMasterTest, GetStories) {
  // Zero stories to should exist.
  bool done{};
  ptr_->GetStories([&](std::vector<std::string> story_names) {
    EXPECT_EQ(0u, story_names.size());
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Create a story.
  session_storage_->CreateStory("foo", /*annotations=*/{});

  // "foo" should be listed.
  done = false;
  ptr_->GetStories([&](std::vector<std::string> story_names) {
    ASSERT_EQ(1u, story_names.size());
    EXPECT_EQ("foo", story_names.at(0));
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

// Verifies that a call to Annotate create a story.
TEST_F(PuppetMasterTest, AnnotateCreatesStory) {
  const auto story_name = "annotate_creates_story";

  auto story = ControlStory(story_name);

  // Create some annotations.
  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_text("test_value");

  auto annotation = fuchsia::modular::Annotation{
      .key = "test_key",
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  // Annotate the story, which should implicitly create it.
  bool done{false};
  story->Annotate(std::move(annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStories should return the newly-created story.
  done = false;
  ptr_->GetStories([&](std::vector<std::string> story_names) {
    ASSERT_EQ(1u, story_names.size());
    EXPECT_EQ(story_name, story_names.at(0));
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

// Verifies that annotations are saved to StoryData.
TEST_F(PuppetMasterTest, AnnotateInStoryData) {
  const auto story_name = "annotate_in_storydata";

  auto story = ControlStory(story_name);

  // Create some annotations, one for each variant of AnnotationValue.
  auto text_annotation_value = fuchsia::modular::AnnotationValue{};
  text_annotation_value.set_text("text_value");
  auto text_annotation = fuchsia::modular::Annotation{
      .key = "text_key",
      .value =
          std::make_unique<fuchsia::modular::AnnotationValue>(fidl::Clone(text_annotation_value))};

  auto bytes_annotation_value = fuchsia::modular::AnnotationValue{};
  bytes_annotation_value.set_bytes({0x01, 0x02, 0x03, 0x04});
  auto bytes_annotation = fuchsia::modular::Annotation{
      .key = "bytes_key",
      .value =
          std::make_unique<fuchsia::modular::AnnotationValue>(fidl::Clone(bytes_annotation_value))};

  fuchsia::mem::Buffer buffer{};
  std::string buffer_value = "buffer_value";
  ASSERT_TRUE(fsl::VmoFromString(buffer_value, &buffer));
  auto buffer_annotation_value = fuchsia::modular::AnnotationValue{};
  buffer_annotation_value.set_buffer(std::move(buffer));
  auto buffer_annotation =
      fuchsia::modular::Annotation{.key = "buffer_key",
                                   .value = std::make_unique<fuchsia::modular::AnnotationValue>(
                                       fidl::Clone(buffer_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(text_annotation));
  annotations.push_back(fidl::Clone(bytes_annotation));
  annotations.push_back(fidl::Clone(buffer_annotation));

  // Annotate the story.
  bool done{false};
  story->Annotate(std::move(annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStoryData should contain the annotations.
  auto story_data = session_storage_->GetStoryData(story_name);
  ASSERT_NE(nullptr, story_data);
  ASSERT_TRUE(story_data->has_story_info());
  EXPECT_TRUE(story_data->story_info().has_annotations());

  EXPECT_EQ(3u, story_data->story_info().annotations().size());

  EXPECT_THAT(story_data->story_info().annotations(),
              UnorderedElementsAre(annotations::AnnotationEq(ByRef(text_annotation)),
                                   annotations::AnnotationEq(ByRef(bytes_annotation)),
                                   annotations::AnnotationEq(ByRef(buffer_annotation))));
}

// Verifies that Annotate merges new annotations, preserving existing ones.
TEST_F(PuppetMasterTest, AnnotateMerge) {
  const auto story_name = "annotate_merge";

  auto story = ControlStory(story_name);

  // Create the initial set of annotations.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key",
      .value =
          std::make_unique<fuchsia::modular::AnnotationValue>(fidl::Clone(first_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(first_annotation));

  // Annotate the story.
  bool done{false};
  story->Annotate(std::move(annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStoryData should contain the first annotation.
  auto story_data = session_storage_->GetStoryData(story_name);
  ASSERT_NE(nullptr, story_data);
  ASSERT_TRUE(story_data->has_story_info());
  EXPECT_TRUE(story_data->story_info().has_annotations());

  {
    const auto annotations = story_data->mutable_story_info()->mutable_annotations();
    EXPECT_EQ(1u, annotations->size());

    EXPECT_EQ(annotations->at(0).key, first_annotation.key);
    EXPECT_EQ(annotations->at(0).value->text(), first_annotation_value.text());
  }

  // Create another set of annotations that should be merged into the initial one.
  auto second_annotation_value = fuchsia::modular::AnnotationValue{};
  second_annotation_value.set_text("second_value");
  auto second_annotation =
      fuchsia::modular::Annotation{.key = "second_key",
                                   .value = std::make_unique<fuchsia::modular::AnnotationValue>(
                                       fidl::Clone(second_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations_2;
  annotations_2.push_back(fidl::Clone(second_annotation));

  // Annotate the story with the second set of annotations.
  done = false;
  story->Annotate(std::move(annotations_2),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStoryData should now return annotations from both the first and second set.
  story_data = session_storage_->GetStoryData(story_name);
  ASSERT_NE(nullptr, story_data);
  ASSERT_TRUE(story_data->has_story_info());
  EXPECT_TRUE(story_data->story_info().has_annotations());
  EXPECT_EQ(2u, story_data->story_info().annotations().size());
  EXPECT_THAT(story_data->story_info().annotations(),
              UnorderedElementsAre(annotations::AnnotationEq(ByRef(first_annotation)),
                                   annotations::AnnotationEq(ByRef(second_annotation))));
}

// Verifies that Annotate returns an error when one of the annotations has a buffer value that
// exceeds MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES.
TEST_F(PuppetMasterTest, AnnotateBufferValueTooBig) {
  const auto story_name = "annotate_buffer_value_too_big";

  auto story = ControlStory(story_name);

  // Create an annotation with a large buffer value.
  fuchsia::mem::Buffer buffer{};
  std::string buffer_value(fuchsia::modular::MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES + 1, 'x');
  ASSERT_TRUE(fsl::VmoFromString(buffer_value, &buffer));

  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_buffer(std::move(buffer));
  auto annotation = fuchsia::modular::Annotation{
      .key = "buffer_key",
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  // Annotate the story.
  bool done{false};
  story->Annotate(std::move(annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_TRUE(result.is_err());
                    EXPECT_EQ(fuchsia::modular::AnnotationError::VALUE_TOO_BIG, result.err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });
}

// Verifies that Annotate returns an error when adding new annotations to exceeds
// MAX_ANNOTATIONS_PER_STORY.
TEST_F(PuppetMasterTest, AnnotateTooMany) {
  // A single Annotate call should not accept more annotations than allowed on a single story.
  ASSERT_GE(fuchsia::modular::MAX_ANNOTATIONS_PER_STORY,
            fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE);

  const auto story_name = "annotate_too_many";

  auto story = ControlStory(story_name);

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
      auto annotation = fuchsia::modular::Annotation{
          .key = "annotation_" + std::to_string(num_annotate_calls) + "_" +
                 std::to_string(num_annotations),
          .value =
              std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};
      annotations.push_back(std::move(annotation));
    }

    // Annotate the story.
    bool done{false};
    story->Annotate(
        std::move(annotations), [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
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
    auto annotation = fuchsia::modular::Annotation{
        .key = "excess_annotation_" + std::to_string(num_annotations),
        .value = std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};
    annotations.push_back(std::move(annotation));
  }

  // Annotate the story.
  bool done{false};
  story->Annotate(
      std::move(annotations), [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS, result.err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that WatchAnnotations returns a NOT_FOUND error if the story does not exist.
TEST_F(PuppetMasterTest, WatchAnnotationsNotFound) {
  constexpr auto story_name = "story_watch_annotations_not_found";

  auto story = ControlStory(story_name);

  bool done{false};
  story->WatchAnnotations([&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(fuchsia::modular::AnnotationError::NOT_FOUND, result.err());
    done = true;
  });

  RunLoopUntil([&] { return done; });
}

// Verifies that WatchAnnotations returns existing annotations on first call.
TEST_F(PuppetMasterTest, WatchAnnotationsExisting) {
  constexpr auto story_name = "story_watch_annotations_existing";

  auto story = ControlStory(story_name);

  // Create a story with some annotations.
  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_text("test_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_key",
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};
  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  session_storage_->CreateStory(story_name, std::move(annotations));

  // Get the annotations.
  bool done{false};
  int annotations_count = 0;
  story->WatchAnnotations([&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
    ASSERT_TRUE(result.is_response());
    annotations_count = result.response().annotations.size();
    done = true;
  });

  RunLoopUntil([&] { return done; });
  EXPECT_EQ(1, annotations_count);
}

// Verifies that WatchAnnotations on two different StoryPuppetMasters both return existing
// annotations on first call.
TEST_F(PuppetMasterTest, WatchAnnotationsExistingMultipleClients) {
  constexpr auto story_name = "story_watch_annotations_existing_multiple_clients";

  auto story = ControlStory(story_name);

  // Create a story with some annotations.
  auto annotation_value = fuchsia::modular::AnnotationValue{};
  annotation_value.set_text("test_value");
  auto annotation = fuchsia::modular::Annotation{
      .key = "test_key",
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(std::move(annotation_value))};
  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(std::move(annotation));

  session_storage_->CreateStory(story_name, std::move(annotations));

  // Get the annotations.
  bool done{false};
  int annotations_count = 0;
  story->WatchAnnotations([&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
    ASSERT_TRUE(result.is_response());
    annotations_count = result.response().annotations.size();
    done = true;
  });

  RunLoopUntil([&] { return done; });
  EXPECT_EQ(1, annotations_count);

  // Get a new StoryPuppetMaster for the same story.
  auto story_2 = ControlStory(story_name);

  // Get the annotations from the second StoryPuppetMaster.
  done = false;
  annotations_count = 0;
  story_2->WatchAnnotations(
      [&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
        ASSERT_TRUE(result.is_response());
        annotations_count = result.response().annotations.size();
        done = true;
      });

  // This should also return the current set of annotations, and not hang for updates.
  RunLoopUntil([&] { return done; });
  EXPECT_EQ(1, annotations_count);
}

// Verifies that WatchAnnotations returns updated annotations on subsequent calls.
TEST_F(PuppetMasterTest, WatchAnnotationsUpdates) {
  constexpr auto story_name = "story_watch_annotations_updates";

  auto story = ControlStory(story_name);

  // Create a story with one annotation.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_test_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_test_key",
      .value =
          std::make_unique<fuchsia::modular::AnnotationValue>(std::move(first_annotation_value))};
  std::vector<fuchsia::modular::Annotation> first_annotations;
  first_annotations.push_back(fidl::Clone(first_annotation));

  session_storage_->CreateStory(story_name, std::move(first_annotations));

  // Get the annotations.
  bool first_watch_called{false};
  std::vector<fuchsia::modular::Annotation> first_watch_annotations;
  story->WatchAnnotations([&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
    // Ensure this callback is only called once.
    ASSERT_FALSE(first_watch_called);
    first_watch_called = true;

    ASSERT_TRUE(result.is_response());
    first_watch_annotations = std::move(result.response().annotations);
  });

  RunLoopUntil([&] { return first_watch_called; });
  EXPECT_THAT(first_watch_annotations,
              ElementsAre(annotations::AnnotationEq(ByRef(first_annotation))));

  // Start watching for annotations.
  bool second_watch_called{false};
  std::vector<fuchsia::modular::Annotation> second_watch_annotations;
  story->WatchAnnotations([&](fuchsia::modular::StoryPuppetMaster_WatchAnnotations_Result result) {
    // Ensure this callback is only called once.
    ASSERT_FALSE(second_watch_called);
    second_watch_called = true;

    ASSERT_TRUE(result.is_response());
    second_watch_annotations = std::move(result.response().annotations);
  });

  // Add another annotation.
  auto second_annotation_value = fuchsia::modular::AnnotationValue{};
  second_annotation_value.set_text("second_test_value");
  auto second_annotation = fuchsia::modular::Annotation{
      .key = "second_test_key",
      .value =
          std::make_unique<fuchsia::modular::AnnotationValue>(std::move(second_annotation_value))};
  std::vector<fuchsia::modular::Annotation> second_annotations;
  second_annotations.push_back(fidl::Clone(second_annotation));

  // Annotate the story.
  bool done{false};
  story->Annotate(std::move(second_annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // WatchAnnotations should have received the fnew annotations.
  RunLoopUntil([&] { return second_watch_called; });
  EXPECT_THAT(second_watch_annotations,
              UnorderedElementsAre(annotations::AnnotationEq(ByRef(first_annotation)),
                                   annotations::AnnotationEq(ByRef(second_annotation))));
}

}  // namespace
}  // namespace modular
