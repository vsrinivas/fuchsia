// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/testing/test_story_command_executor.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

#define TEST_NAME(SUFFIX) \
  std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" #SUFFIX;

namespace modular {
namespace {

using ::testing::ByRef;
using ::testing::Pointee;
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
    session_storage_ = MakeSessionStorage("page");
    impl_ = std::make_unique<PuppetMasterImpl>(session_storage_.get(), &executor_);
    impl_->Connect(ptr_.NewRequest());
  }

  fuchsia::modular::StoryPuppetMasterPtr ControlStory(fidl::StringPtr story_name) {
    story_name_ = std::move(story_name);
    fuchsia::modular::StoryPuppetMasterPtr ptr;
    ptr_->ControlStory(story_name_.value_or(""), ptr.NewRequest());
    return ptr;
  }

  void EnqueueAddModCommand(const fuchsia::modular::StoryPuppetMasterPtr& story,
                            const std::string& module_name) {
    auto story_name = story_name_.value_or("");
    FXL_CHECK(story_name.size() > 0);
    // Add the module.
    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(MakeAddModCommand(module_name));
    story->Enqueue(std::move(commands));

    // Instruct our test executor to return an OK status, and since we're going to
    // AddMod, give the executor a StoryStorage.
    executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
    executor_.SetStoryStorage(GetStoryStorage(session_storage_.get(), story_name));
  }

 protected:
  fidl::StringPtr story_name_;
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
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr);
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

// Verifies that calls to SetStoryInfoExtra after the story is created
// do not modify the original value.
TEST_F(PuppetMasterTest, SetStoryInfoExtraAfterCreateStory) {
  const auto story_name = "story_info_extra_2";

  auto story = ControlStory(story_name);

  // Enqueue some commands.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  // The story, and its StoryData, does not exist until the story is created,
  // which is after the commands are executed.
  bool done{};
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_EQ(nullptr, data);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Execute the commands, implicitly creating the story.
  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });
  auto story_id = executor_.last_story_id();

  // Calling SetStoryInfoExtra again and executing again should not modify
  // the original, unset value.
  std::vector<fuchsia::modular::StoryInfoExtraEntry> extra_info{
      fuchsia::modular::StoryInfoExtraEntry{
          .key = "ignored_key",
          .value = "ignored_value",
      }};

  // Try to SetStoryInfoExtra. It should not return an error even though the story has
  // already been created, since the method is a no-op.
  done = false;
  story->SetStoryInfoExtra(
      std::move(extra_info),
      [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_FALSE(result.is_err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that calls to SetStoryInfoExtra succeed after a story is deleted.
TEST_F(PuppetMasterTest, SetStoryInfoExtraAfterDeleteStory) {
  const auto story_name = "story_info_extra_3";

  // Create the story.
  bool done{};
  session_storage_->CreateStory(story_name, /*annotations=*/{})
      ->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) { done = true; });
  RunLoopUntil([&] { return done; });

  const std::vector<fuchsia::modular::StoryInfoExtraEntry> kStoryExtraInfo{
      fuchsia::modular::StoryInfoExtraEntry{
          .key = "ignored_key",
          .value = "ignored_value",
      }};

  // Try to SetStoryInfoExtra. It should not return an error even though the story has
  // already been created, since the method is a no-op.
  auto story = ControlStory(story_name);
  done = false;
  story->SetStoryInfoExtra(
      kStoryExtraInfo, [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_FALSE(result.is_err());
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Delete the story.
  done = false;
  ptr_->DeleteStory(story_name, [&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Try to SetStoryInfoExtra again. It should succeed because story it applies
  // to has not been created yet.
  story = ControlStory(story_name);
  done = false;
  story->SetStoryInfoExtra(
      kStoryExtraInfo, [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_FALSE(result.is_err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(PuppetMasterTest, DeleteStory) {
  std::string story_id;

  // Create a story.
  session_storage_->CreateStory("foo", /*annotations=*/{})
      ->Then(
          [&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) { story_id = id.value_or(""); });

  // Delete it
  bool done{};
  ptr_->DeleteStory("foo", [&] { done = true; });
  RunLoopUntil([&] { return done; });

  done = false;
  session_storage_->GetStoryData(story_id)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_EQ(story_data, nullptr);
        done = true;
      });

  RunLoopUntil([&] { return done; });
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
  impl_->DeleteStory(kStoryName, [](){});

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
      .key = "test_key", .value = fidl::MakeOptional(std::move(annotation_value))};

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
  bool done{false};
  story->Annotate(std::move(annotations),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStoryData should contain the annotations.
  done = false;
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        ASSERT_TRUE(story_data->has_story_info());
        EXPECT_TRUE(story_data->story_info().has_annotations());

        const auto annotations = story_data->mutable_story_info()->mutable_annotations();
        EXPECT_EQ(3u, annotations->size());

        EXPECT_THAT(annotations, Pointee(UnorderedElementsAre(
                                     annotations::AnnotationEq(ByRef(text_annotation)),
                                     annotations::AnnotationEq(ByRef(bytes_annotation)),
                                     annotations::AnnotationEq(ByRef(buffer_annotation)))));

        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that Annotate merges new annotations, preserving existing ones.
TEST_F(PuppetMasterTest, AnnotateMerge) {
  const auto story_name = "annotate_merge";

  auto story = ControlStory(story_name);

  // Create the initial set of annotations.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key", .value = fidl::MakeOptional(fidl::Clone(first_annotation_value))};

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
  done = false;
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        ASSERT_TRUE(story_data->has_story_info());
        EXPECT_TRUE(story_data->story_info().has_annotations());

        const auto annotations = story_data->mutable_story_info()->mutable_annotations();
        EXPECT_EQ(1u, annotations->size());

        EXPECT_EQ(annotations->at(0).key, first_annotation.key);
        EXPECT_EQ(annotations->at(0).value->text(), first_annotation_value.text());

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
  story->Annotate(std::move(annotations_2),
                  [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                    EXPECT_FALSE(result.is_err());
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // GetStoryData should now return annotations from both the first and second set.
  done = false;
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        ASSERT_TRUE(story_data->has_story_info());
        EXPECT_TRUE(story_data->story_info().has_annotations());

        const auto annotations = story_data->mutable_story_info()->mutable_annotations();
        EXPECT_EQ(2u, annotations->size());

        EXPECT_THAT(annotations, Pointee(UnorderedElementsAre(
                                     annotations::AnnotationEq(ByRef(first_annotation)),
                                     annotations::AnnotationEq(ByRef(second_annotation)))));

        done = true;
      });
  RunLoopUntil([&] { return done; });
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
      .key = "buffer_key", .value = fidl::MakeOptional(std::move(annotation_value))};

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
      auto annotation =
          fuchsia::modular::Annotation{.key = "annotation_" + std::to_string(num_annotate_calls) +
                                              "_" + std::to_string(num_annotations),
                                       .value = fidl::MakeOptional(std::move(annotation_value))};
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
    auto annotation =
        fuchsia::modular::Annotation{.key = "excess_annotation_" + std::to_string(num_annotations),
                                     .value = fidl::MakeOptional(std::move(annotation_value))};
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

// Verifies that annotations are saved to ModuleData.
TEST_F(PuppetMasterTest, AnnotateInModuleDataAllVariants) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  RunLoopUntil([&]() { return add_mod_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

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

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_FALSE(result.is_err());
                          annotate_done = true;
                        });
  RunLoopUntil([&] { return annotate_done; });

  // Get the matching module and confirm it has the annotations we added
  bool read_done = false;
  auto story_storage = GetStoryStorage(session_storage_.get(), story_name);
  // Note the story_storage unique_ptr lifetime must extend through the RunLoopUntil() invocation
  story_storage->ReadModuleData({module_name})
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        read_done = true;  // ensure loop exits before ASSERTs
        ASSERT_NE(nullptr, module_data);
        EXPECT_TRUE(module_data->has_annotations());

        const auto annotations = module_data->mutable_annotations();
        EXPECT_EQ(3u, annotations->size());

        EXPECT_THAT(annotations, Pointee(UnorderedElementsAre(
                                     annotations::AnnotationEq(ByRef(text_annotation)),
                                     annotations::AnnotationEq(ByRef(bytes_annotation)),
                                     annotations::AnnotationEq(ByRef(buffer_annotation)))));
      });
  RunLoopUntil([&] { return read_done; });
}

// Verifies that annotations are saved to ModuleData, even if the ModuleData shows up in storage
// after attempting to add the annotations.
TEST_F(PuppetMasterTest, AnnotateInModuleDataWithoutWaiting) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  // Create a test annotation.
  auto text_annotation_value = fuchsia::modular::AnnotationValue{};
  text_annotation_value.set_text("text_value");
  auto text_annotation = fuchsia::modular::Annotation{
      .key = "text_key", .value = fidl::MakeOptional(fidl::Clone(text_annotation_value))};
  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(text_annotation));

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_FALSE(result.is_err());
                          annotate_done = true;
                        });

  RunLoopUntil([&] { return add_mod_done && annotate_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

  // Get the matching module and confirm it has the annotations we added
  bool read_done = false;
  auto story_storage = GetStoryStorage(session_storage_.get(), story_name);
  // Note the story_storage unique_ptr lifetime must extend through the RunLoopUntil() invocation
  story_storage->ReadModuleData({module_name})
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        read_done = true;  // ensure loop exits before ASSERTs
        ASSERT_NE(nullptr, module_data);
        ASSERT_TRUE(module_data->has_annotations());
        const auto annotations = module_data->mutable_annotations();
        ASSERT_EQ(1u, annotations->size());
        EXPECT_EQ(annotations->at(0).key, text_annotation.key);
        EXPECT_EQ(annotations->at(0).value->text(), text_annotation_value.text());
      });
  RunLoopUntil([&] { return read_done; });
}

// Verifies that annotations are saved to ModuleData, even if the Module is added after the
// annotations are posted.
TEST_F(PuppetMasterTest, AnnotateInModuleDataBeforeAddMod) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  // Create a test annotation.
  auto text_annotation_value = fuchsia::modular::AnnotationValue{};
  text_annotation_value.set_text("text_value");
  auto text_annotation = fuchsia::modular::Annotation{
      .key = "text_key", .value = fidl::MakeOptional(fidl::Clone(text_annotation_value))};
  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(text_annotation));

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_FALSE(result.is_err());
                          annotate_done = true;
                        });

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  RunLoopUntil([&] { return add_mod_done && annotate_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

  // Get the matching module and confirm it has the annotations we added
  bool read_done = false;
  auto story_storage = GetStoryStorage(session_storage_.get(), story_name);
  // Note the story_storage unique_ptr lifetime must extend through the RunLoopUntil() invocation
  story_storage->ReadModuleData({module_name})
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        read_done = true;  // ensure loop exits before ASSERTs
        ASSERT_NE(nullptr, module_data);
        ASSERT_TRUE(module_data->has_annotations());
        const auto annotations = module_data->mutable_annotations();
        ASSERT_EQ(1u, annotations->size());
        EXPECT_EQ(annotations->at(0).key, text_annotation.key);
        EXPECT_EQ(annotations->at(0).value->text(), text_annotation_value.text());
      });
  RunLoopUntil([&] { return read_done; });
}

// Verifies the NOT_FOUND error is returned if attempting to annotate a module without first having
// a story (and required StoryStorage).
TEST_F(PuppetMasterTest, AnnotateModuleWithoutCreatingStory) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  // Create a test annotation.
  auto text_annotation_value = fuchsia::modular::AnnotationValue{};
  text_annotation_value.set_text("text_value");
  auto text_annotation = fuchsia::modular::Annotation{
      .key = "text_key", .value = fidl::MakeOptional(fidl::Clone(text_annotation_value))};
  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(text_annotation));

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_TRUE(result.is_err());
                          EXPECT_EQ(fuchsia::modular::AnnotationError::NOT_FOUND, result.err());
                          annotate_done = true;
                        });
}

// Verifies that Annotate merges new annotations, preserving existing ones.
TEST_F(PuppetMasterTest, AnnotateMergeInModuleData) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  RunLoopUntil([&]() { return add_mod_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

  // Create the initial set of annotations.
  auto first_annotation_value = fuchsia::modular::AnnotationValue{};
  first_annotation_value.set_text("first_value");
  auto first_annotation = fuchsia::modular::Annotation{
      .key = "first_key", .value = fidl::MakeOptional(fidl::Clone(first_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fidl::Clone(first_annotation));

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_FALSE(result.is_err());
                          annotate_done = true;
                        });
  RunLoopUntil([&] { return annotate_done; });

  // Get the matching module and confirm it has the annotations we added
  bool read_done = false;
  auto story_storage = GetStoryStorage(session_storage_.get(), story_name);
  // Note the story_storage unique_ptr lifetime must extend through the RunLoopUntil() invocation
  story_storage->ReadModuleData({module_name})
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        read_done = true;  // ensure loop exits before ASSERTs
        ASSERT_NE(nullptr, module_data);
        EXPECT_TRUE(module_data->has_annotations());

        const auto annotations = module_data->mutable_annotations();
        EXPECT_EQ(1u, annotations->size());

        EXPECT_EQ(annotations->at(0).key, first_annotation.key);
        EXPECT_EQ(annotations->at(0).value->text(), first_annotation_value.text());
      });

  RunLoopUntil([&] { return read_done; });

  // Create another set of annotations that should be merged into the initial one.
  auto second_annotation_value = fuchsia::modular::AnnotationValue{};
  second_annotation_value.set_text("second_value");
  auto second_annotation = fuchsia::modular::Annotation{
      .key = "second_key", .value = fidl::MakeOptional(fidl::Clone(second_annotation_value))};

  std::vector<fuchsia::modular::Annotation> annotations_2;
  annotations_2.push_back(fidl::Clone(second_annotation));

  // Annotate the story with the second set of annotations.
  annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations_2),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_FALSE(result.is_err());
                          annotate_done = true;
                        });
  RunLoopUntil([&] { return annotate_done; });

  // ReadModuleData should now return annotations from both the first and second set.
  read_done = false;
  story_storage->ReadModuleData({module_name})
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        ASSERT_NE(nullptr, module_data);
        EXPECT_TRUE(module_data->has_annotations());
        const auto annotations = module_data->mutable_annotations();
        EXPECT_EQ(2u, annotations->size());
        EXPECT_THAT(annotations, Pointee(UnorderedElementsAre(
                                     annotations::AnnotationEq(ByRef(first_annotation)),
                                     annotations::AnnotationEq(ByRef(second_annotation)))));
        read_done = true;
      });
  RunLoopUntil([&] { return read_done; });
}

// Verifies that AnnotateModule returns an error when one of the annotations has a buffer value that
// exceeds MAX_ANNOTATION_VALUE_BUFFER_LENGTH_BYTES.
TEST_F(PuppetMasterTest, AnnotateModuleBufferValueTooBig) {
  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  RunLoopUntil([&]() { return add_mod_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

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

  // Annotate the module.
  bool annotate_done = false;
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_TRUE(result.is_err());
                          EXPECT_EQ(fuchsia::modular::AnnotationError::VALUE_TOO_BIG, result.err());
                          annotate_done = true;
                        });
  RunLoopUntil([&] { return annotate_done; });
}

// Verifies that AnnotateModule returns an error when adding new annotations to exceeds
// MAX_ANNOTATIONS_PER_MODULE.
TEST_F(PuppetMasterTest, AnnotateModuleTooMany) {
  // A single Annotate call should not accept more annotations than allowed on a single story.
  ASSERT_GE(fuchsia::modular::MAX_ANNOTATIONS_PER_MODULE,
            fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE);

  const auto story_name = TEST_NAME(story);
  const auto module_name = TEST_NAME(module);

  // Allocate story_storage for the story
  CreateStory(story_name, session_storage_.get());

  // Get a StoryPuppetMaster
  auto story = ControlStory(story_name);

  EnqueueAddModCommand(story, module_name);

  fuchsia::modular::ExecuteResult result;
  bool add_mod_done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    add_mod_done = true;
  });

  RunLoopUntil([&]() { return add_mod_done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(story_name, executor_.last_story_id().value_or(""));

  // Annotate the story repeatedly, in batches of MAX_ANNOTATIONS_PER_UPDATE items, in order
  // to reach, but not exceed the MAX_ANNOTATIONS_PER_MODULE limit.
  for (unsigned int num_annotate_calls = 0;
       num_annotate_calls <
       fuchsia::modular::MAX_ANNOTATIONS_PER_MODULE / fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE;
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
    bool annotate_done{false};
    story->AnnotateModule(module_name, std::move(annotations),
                          [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                            EXPECT_FALSE(result.is_err())
                                << "AnnotateModule call #" << num_annotate_calls
                                << " returned an error when trying to add "
                                << std::to_string(fuchsia::modular::MAX_ANNOTATIONS_PER_UPDATE)
                                << " annotations to the story.";
                            annotate_done = true;
                          });
    RunLoopUntil([&] { return annotate_done; });
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
  bool annotate_done{false};
  story->AnnotateModule(module_name, std::move(annotations),
                        [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
                          EXPECT_TRUE(result.is_err());
                          EXPECT_EQ(fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS,
                                    result.err());
                          annotate_done = true;
                        });
  RunLoopUntil([&] { return annotate_done; });
}

}  // namespace
}  // namespace modular
