// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/tts/tts_manager.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <vector>

namespace a11y {
namespace {

// Fake engine class to listen for incoming requests by the Tts Manager.
class FakeEngine : public fuchsia::accessibility::tts::Engine {
 public:
  FakeEngine() = default;
  ~FakeEngine() = default;

  fidl::InterfaceHandle<fuchsia::accessibility::tts::Engine> GetHandle() {
    return bindings_.AddBinding(this);
  }

  // Disconnects this fake Engine. All bindings are close.
  void Disconnect() { return bindings_.CloseAll(); }

  // Examine the utterances received via Enqueue() calls.
  const std::vector<fuchsia::accessibility::tts::Utterance>& ExamineUtterances() const {
    return utterances_;
  }

  // Returns true if a call to Cancel() was made to this object. False otherwise.
  bool ReceivedCancel() const { return received_cancel_; }

  // Returns true if a call to Speak() was made to this object. False otherwise.
  bool ReceivedSpeak() const { return received_speak_; }

 private:
  // |fuchsia.accessibility.tts.Engine|
  void Enqueue(fuchsia::accessibility::tts::Utterance utterance,
               EnqueueCallback callback) override {
    utterances_.emplace_back(std::move(utterance));
    fuchsia::accessibility::tts::Engine_Enqueue_Result result;
    result.set_response(fuchsia::accessibility::tts::Engine_Enqueue_Response{});
    callback(std::move(result));
  }

  // |fuchsia.accessibility.tts.Engine|
  void Speak(SpeakCallback callback) override {
    received_speak_ = true;
    utterances_.clear();
    fuchsia::accessibility::tts::Engine_Speak_Result result;
    result.set_response(fuchsia::accessibility::tts::Engine_Speak_Response{});
    callback(std::move(result));
  }

  // |fuchsia.accessibility.tts.Engine|
  void Cancel(CancelCallback callback) override {
    received_cancel_ = true;
    utterances_.clear();
    callback();
  }

  fidl::BindingSet<fuchsia::accessibility::tts::Engine> bindings_;

  // Utterances received via Enqueue() calls.
  std::vector<fuchsia::accessibility::tts::Utterance> utterances_;
  // Weather  a Cancel() call was made.
  bool received_cancel_ = false;
  // Weather a Speak() call was made.
  bool received_speak_ = true;
};

class TtsManagerTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    startup_context_ = sys::ComponentContext::Create();
    tts_manager_ = std::make_unique<TtsManager>(startup_context_.get());
  }

  std::unique_ptr<sys::ComponentContext> startup_context_;
  std::unique_ptr<TtsManager> tts_manager_;
};

TEST_F(TtsManagerTest, RegistersOnlyOneSpeaker) {
  // This test makes sure that only one speaker can start using a Tts Engine.
  fuchsia::accessibility::tts::EnginePtr speaker_1;
  tts_manager_->OpenEngine(speaker_1.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             EXPECT_TRUE(result.is_response());
                           });
  RunLoopUntilIdle();
  // Attempts to connect a second speaker will fail.
  fuchsia::accessibility::tts::EnginePtr speaker_2;
  tts_manager_->OpenEngine(speaker_2.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             EXPECT_TRUE(result.is_err());
                             EXPECT_EQ(fuchsia::accessibility::tts::Error::BUSY, result.err());
                           });
  RunLoopUntilIdle();
}

TEST_F(TtsManagerTest, RegistersOnlyOneEngine) {
  // This test makes sure that only one engine can register itself with the Tts manager.
  FakeEngine fake_engine_1;
  auto engine_handle_1 = fake_engine_1.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle_1),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();
  // Attempts to connect a second speaker will fail.
  FakeEngine fake_engine_2;
  auto engine_handle_2 = fake_engine_2.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle_2),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(fuchsia::accessibility::tts::Error::BUSY, result.err());
      });
  RunLoopUntilIdle();
}

TEST_F(TtsManagerTest, ForwardsEngineOperations) {
  // This test makes sure that once there is a speaker and an engine registered,
  // the operations requested by the speaker are forwarded to the engine.
  fuchsia::accessibility::tts::EnginePtr speaker;
  tts_manager_->OpenEngine(speaker.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             EXPECT_TRUE(result.is_response());
                           });
  RunLoopUntilIdle();
  // Now, registers the fake engine.
  FakeEngine fake_engine;
  auto engine_handle = fake_engine.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();
  fuchsia::accessibility::tts::Utterance utterance;
  utterance.set_message("hello world");
  speaker->Enqueue(std::move(utterance), [](auto) {});
  RunLoopUntilIdle();
  // Examine sent utterance.
  EXPECT_EQ(fake_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(fake_engine.ExamineUtterances()[0].message(), "hello world");
  speaker->Speak([](auto) {});
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_engine.ReceivedSpeak());
  speaker->Cancel([]() {});
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_engine.ReceivedCancel());
}

TEST_F(TtsManagerTest, FailsWhenThereIsNoEngine) {
  // this test makes sure that Engine operations fail when there is no Engine
  // registered.
  fuchsia::accessibility::tts::EnginePtr speaker;
  tts_manager_->OpenEngine(speaker.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             EXPECT_TRUE(result.is_response());
                           });
  RunLoopUntilIdle();
  // Now, calls some Engine operations. They should fail.
  {
    fuchsia::accessibility::tts::Utterance utterance;
    utterance.set_message("hello world");
    speaker->Enqueue(std::move(utterance), [](auto result) {
      EXPECT_TRUE(result.is_err());
      EXPECT_EQ(fuchsia::accessibility::tts::Error::BAD_STATE, result.err());
    });
  }
  RunLoopUntilIdle();

  // Now, registers the fake engine.
  FakeEngine fake_engine;
  auto engine_handle = fake_engine.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();
  {
    fuchsia::accessibility::tts::Utterance utterance;
    utterance.set_message("hello world");
    speaker->Enqueue(std::move(utterance), [](auto) {});
  }
  RunLoopUntilIdle();
  // Examine sent utterance.
  EXPECT_EQ(fake_engine.ExamineUtterances().size(), 1u);
  EXPECT_EQ(fake_engine.ExamineUtterances()[0].message(), "hello world");
  speaker->Speak([](auto) {});
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_engine.ReceivedSpeak());
  speaker->Cancel([]() {});
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_engine.ReceivedCancel());

  // Disconnects the Engine.
  fake_engine.Disconnect();

  // Incoming requests should fail, as there is no Engine registered.
  {
    fuchsia::accessibility::tts::Utterance utterance;
    utterance.set_message("hello world");
    speaker->Enqueue(std::move(utterance), [](auto result) {
      EXPECT_TRUE(result.is_err());
      EXPECT_EQ(fuchsia::accessibility::tts::Error::BAD_STATE, result.err());
    });
  }
  RunLoopUntilIdle();

  // Finally, registers a second Engine.
  FakeEngine fake_engine_new;
  auto engine_handle_new = fake_engine_new.GetHandle();
  tts_manager_->RegisterEngine(
      std::move(engine_handle_new),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();
  {
    fuchsia::accessibility::tts::Utterance utterance;
    utterance.set_message("hello world new");
    speaker->Enqueue(std::move(utterance), [](auto) {});
  }
  RunLoopUntilIdle();
  // Examine sent utterance.
  EXPECT_EQ(fake_engine_new.ExamineUtterances().size(), 1u);
  EXPECT_EQ(fake_engine_new.ExamineUtterances()[0].message(), "hello world new");
}

}  // namespace
}  // namespace a11y
