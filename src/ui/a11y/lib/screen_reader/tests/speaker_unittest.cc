// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/speaker.h"

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_node_describer.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Node;

class SpeakerTest : public gtest::RealLoopFixture {
 public:
  SpeakerTest()
      : context_provider_(),
        tts_manager_(context_provider_.context()),
        executor_(async_get_default_dispatcher()) {
    fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> engine_handle =
        mock_tts_engine_.GetHandle();
    tts_manager_.RegisterEngine(
        std::move(engine_handle),
        [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
          EXPECT_TRUE(result.is_response());
        });
    RunLoopUntilIdle();
    tts_manager_.OpenEngine(tts_engine_ptr_.NewRequest(),
                            [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                              EXPECT_TRUE(result.is_response());
                            });
    RunLoopUntilIdle();

    auto node_describer = std::make_unique<MockNodeDescriber>();
    mock_node_describer_ptr_ = node_describer.get();
    speaker_ = std::make_unique<a11y::Speaker>(tts_engine_ptr_, std::move(node_describer));
  }
  ~SpeakerTest() = default;

 protected:
  std::unique_ptr<a11y::Speaker> speaker_;
  fuchsia::accessibility::tts::EnginePtr tts_engine_ptr_;
  MockNodeDescriber* mock_node_describer_ptr_;
  MockTtsEngine mock_tts_engine_;
  sys::testing::ComponentContextProvider context_provider_;
  a11y::TtsManager tts_manager_;
  async::Executor executor_;
};

TEST_F(SpeakerTest, SpeaksANode) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::UNKNOWN);
  auto task = speaker_->SpeakPromise(&node, {.interrupt = true});
  executor_.schedule_task(std::move(task));
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "foo");
}

TEST_F(SpeakerTest, SpeaksANodeRightAwayWhenFrontOfTheQueue) {
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::UNKNOWN);
  // Interrupt here is false, which means that this task would wait for others to finish. As it is
  // at the front of the queue, it starts right away.
  auto task = speaker_->SpeakPromise(&node, {.interrupt = false});
  executor_.schedule_task(std::move(task));
  RunLoopUntilIdle();
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "foo");
}

TEST_F(SpeakerTest, SpeaksANodeWithTimeSpacedUtterances) {
  std::vector<a11y::NodeDescriber::UtteranceAndContext> description;
  a11y::NodeDescriber::UtteranceAndContext utterance1;
  utterance1.utterance.set_message("foo");
  a11y::NodeDescriber::UtteranceAndContext utterance2;
  utterance2.utterance.set_message("button");
  utterance2.delay = zx::duration(zx::msec(300));
  description.push_back(std::move(utterance1));
  description.push_back(std::move(utterance2));
  mock_node_describer_ptr_->set_description(std::move(description));
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  auto task = speaker_->SpeakPromise(&node, {.interrupt = true});
  executor_.schedule_task(std::move(task));
  RunLoopWithTimeout(zx::duration(zx::msec(100)));
  // At this point, the first utterance ran, but the second is still waiting the 300 msec delay to
  // elapse.
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "foo");
  RunLoopWithTimeout(zx::duration(zx::msec(300)));
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 2u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[1].message(), "button");
}

TEST_F(SpeakerTest, TaskWaitsInQueueWhenNotInterrupting) {
  std::vector<a11y::NodeDescriber::UtteranceAndContext> description;
  a11y::NodeDescriber::UtteranceAndContext utterance1;
  utterance1.utterance.set_message("foo");
  a11y::NodeDescriber::UtteranceAndContext utterance2;
  utterance2.utterance.set_message("button");
  utterance2.delay = zx::duration(zx::msec(300));
  description.push_back(std::move(utterance1));
  description.push_back(std::move(utterance2));
  mock_node_describer_ptr_->set_description(std::move(description));
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  auto task = speaker_->SpeakPromise(&node, {.interrupt = true});
  // Creates a new task that will not run right away because it is not interrupting.
  // Note that the second task will also call the mock node describer, but will only receive "foo"in
  // return.
  auto task2 = speaker_->SpeakPromise(&node, {.interrupt = false});
  executor_.schedule_task(std::move(task));
  executor_.schedule_task(std::move(task2));
  RunLoopWithTimeout(zx::duration(zx::msec(100)));
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "foo");
  RunLoopWithTimeout(zx::duration(zx::msec(300)));
  // Now, the first task ran and notified the second it can start running. Check if utterances were
  // received in order.
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 3u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[1].message(), "button");
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[2].message(), "foo");
}

TEST_F(SpeakerTest, TaskTrumpsOtherTasksWhenInterrupting) {
  std::vector<a11y::NodeDescriber::UtteranceAndContext> description;
  a11y::NodeDescriber::UtteranceAndContext utterance1;
  utterance1.utterance.set_message("foo");
  a11y::NodeDescriber::UtteranceAndContext utterance2;
  utterance2.utterance.set_message("button");
  utterance2.delay = zx::duration(zx::msec(300));
  description.push_back(std::move(utterance1));
  description.push_back(std::move(utterance2));
  mock_node_describer_ptr_->set_description(std::move(description));
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
  Node node2;
  node2.mutable_attributes()->set_label("bar");
  auto task = speaker_->SpeakPromise(&node, {.interrupt = false});
  auto task2 = speaker_->SpeakPromise(&node2, {.interrupt = true});

  executor_.schedule_task(std::move(task));
  RunLoopWithTimeout(zx::duration(zx::msec(100)));
  EXPECT_TRUE(mock_tts_engine_.ReceivedSpeak());
  EXPECT_FALSE(mock_tts_engine_.ReceivedCancel());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "foo");
  // Runs the second task, which causes the first to be canceled in flight.
  executor_.schedule_task(std::move(task2));
  RunLoopWithTimeout(zx::duration(zx::msec(300)));
  // The first task did not have the time to speak "button". Note that since the second task
  // cancels the first, a Cancel() call is also made to the tts engine, which clears its internal
  // state for a new set of utterances.
  EXPECT_TRUE(mock_tts_engine_.ReceivedCancel());
  ASSERT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances()[0].message(), "bar");
}

TEST_F(SpeakerTest, DropsTaskWhenEnqueueFails) {
  mock_tts_engine_.set_should_fail_enqueue(true);
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::UNKNOWN);
  auto task = speaker_->SpeakPromise(&node, {.interrupt = true});
  executor_.schedule_task(std::move(task));
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
  EXPECT_TRUE(mock_tts_engine_.ExamineUtterances().empty());
}

TEST_F(SpeakerTest, DropsTaskWhenSpeakFails) {
  mock_tts_engine_.set_should_fail_speak(true);
  Node node;
  node.mutable_attributes()->set_label("foo");
  node.set_role(fuchsia::accessibility::semantics::Role::UNKNOWN);
  auto task = speaker_->SpeakPromise(&node, {.interrupt = true});
  executor_.schedule_task(std::move(task));
  RunLoopUntilIdle();
  EXPECT_FALSE(mock_tts_engine_.ReceivedSpeak());
  // Unlike when the enqueue fails, this received a single utterance.
  EXPECT_EQ(mock_tts_engine_.ExamineUtterances().size(), 1u);
}

}  // namespace
}  // namespace accessibility_test
