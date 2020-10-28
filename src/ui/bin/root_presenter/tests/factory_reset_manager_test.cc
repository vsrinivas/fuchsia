// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/factory_reset_manager.h"

#include <fuchsia/media/sounds/cpp/fidl_test_base.h>
#include <fuchsia/recovery/cpp/fidl_test_base.h>
#include <fuchsia/recovery/policy/cpp/fidl.h>
#include <lib/async/time.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/status.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace root_presenter {
namespace testing {

using fuchsia::media::AudioRenderUsage;
using fuchsia::media::sounds::Player_AddSoundFromFile_Response;
using fuchsia::media::sounds::Player_AddSoundFromFile_Result;
using fuchsia::media::sounds::Player_PlaySound_Response;
using fuchsia::media::sounds::Player_PlaySound_Result;
using fuchsia::media::sounds::PlaySoundError;
using fuchsia::media::sounds::testing::Player_TestBase;
using ::testing::ByMove;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Return;

constexpr char kFactoryResetDisallowed[] = "/data/factory_reset_disallowed";

class FakeFactoryReset : public fuchsia::recovery::testing::FactoryReset_TestBase {
 public:
  FakeFactoryReset() : fuchsia::recovery::testing::FactoryReset_TestBase() {}
  FakeFactoryReset(MockFunction<void(std::string check_point_name)>* check)
      : fuchsia::recovery::testing::FactoryReset_TestBase(), check_(check) {}

  void NotImplemented_(const std::string& name) final {}

  fidl::InterfaceRequestHandler<fuchsia::recovery::FactoryReset> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::recovery::FactoryReset> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  void Reset(ResetCallback callback) override {
    if (check_ != nullptr) {
      check_->Call("Reset");
    }
    callback(ZX_OK);
    triggered_ = true;
  }

  bool triggered() const { return triggered_; }

 private:
  bool triggered_ = false;
  MockFunction<void(std::string check_point_name)>* check_ = nullptr;

  fidl::BindingSet<fuchsia::recovery::FactoryReset> bindings_;
};

class FactoryResetManagerTest : public gtest::TestLoopFixture {
 public:
  explicit FactoryResetManagerTest(bool is_factory_reset_allowed) {
    if (!is_factory_reset_allowed) {
      EXPECT_TRUE(files::WriteFile(kFactoryResetDisallowed, ""));
    }

    factory_reset_manager_ = std::make_unique<FactoryResetManager>(
        *context_provider_.context(), std::make_shared<MediaRetriever>());
    context_provider_.service_directory_provider()->AddService(factory_reset_.GetHandler());

    context_provider_.ConnectToPublicService<fuchsia::recovery::policy::Device>(
        policy_ptr_.NewRequest());
    policy_ptr_.set_error_handler([](auto...) { FAIL(); });
    context_provider_.ConnectToPublicService<fuchsia::recovery::ui::FactoryResetCountdown>(
        countdown_ptr_.NewRequest());
    countdown_ptr_.set_error_handler([](auto...) { FAIL(); });
  }

  FactoryResetManagerTest() : FactoryResetManagerTest(true) {}

  ~FactoryResetManagerTest() { files::DeletePath(kFactoryResetDisallowed, /* recursive= */ false); }

  bool triggered() const { return factory_reset_.triggered(); }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<FactoryResetManager> factory_reset_manager_;
  FakeFactoryReset factory_reset_;

  fuchsia::recovery::policy::DevicePtr policy_ptr_;
  fuchsia::recovery::ui::FactoryResetCountdownPtr countdown_ptr_;
};

// Used to test the behavior of FactoryResetManager when the initial policy is "DISALLOWED".
class FactoryResetManagerTestWithResetInitiallyDisallowed : public FactoryResetManagerTest {
 public:
  FactoryResetManagerTestWithResetInitiallyDisallowed() : FactoryResetManagerTest(false) {}
};

TEST_F(FactoryResetManagerTest, ProcessingMediaButtons) {
  fuchsia::ui::input::MediaButtonsReport report;
  report.volume_up = true;
  EXPECT_FALSE(factory_reset_manager_->OnMediaButtonReport(report));

  fuchsia::ui::input::MediaButtonsReport report2;
  report2.volume_down = true;
  EXPECT_FALSE(factory_reset_manager_->OnMediaButtonReport(report2));

  fuchsia::ui::input::MediaButtonsReport report3;
  report3.volume_up = true;
  report3.volume_down = true;
  EXPECT_FALSE(factory_reset_manager_->OnMediaButtonReport(report3));

  fuchsia::ui::input::MediaButtonsReport report4;
  report4.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report4));
}

TEST_F(FactoryResetManagerTest, FactoryResetDisallowed) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());
}

TEST_F(FactoryResetManagerTest, FactoryResetAllowedThenDisallowed) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  policy_ptr_->SetIsLocalResetAllowed(true);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());
}

TEST_F(FactoryResetManagerTest, FactoryResetDisallowedDuringButtonCountdown) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  // Factory reset should cancel if the policy is disallowed.
  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetDisallowedBeforePressing) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  // Factory reset should cancel if the policy is disallowed.
  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());
}

TEST_F(FactoryResetManagerTest, FactoryResetButtonDisallowedDuringResetCountdown) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kButtonCountdownDuration);
  EXPECT_EQ(FactoryResetState::RESET_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  // Factory reset should cancel if the policy is disallowed.
  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetButtonPressedAndReleasedDuringDelayCountdown) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  // Factory reset should cancel if the button is released.
  report.reset = false;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetButtonPressedAndReleasedDuringResetCountdown) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kButtonCountdownDuration);
  EXPECT_EQ(FactoryResetState::RESET_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  // Factory reset should cancel if the button is released.
  report.reset = false;
  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetButtonHeldAndTrigger) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;

  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kButtonCountdownDuration);
  EXPECT_EQ(FactoryResetState::RESET_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_->factory_reset_state());
}

TEST_F(FactoryResetManagerTest, BroadcastCountdown) {
  fuchsia::recovery::ui::FactoryResetCountdownState state;

  bool watchReturned = false;

  auto watchCallback =
      [&state, &watchReturned](fuchsia::recovery::ui::FactoryResetCountdownState newState) {
        watchReturned = true;
        newState.Clone(&state);
      };

  countdown_ptr_->Watch(watchCallback);
  RunLoopUntilIdle();

  // Initial watch should return immediately, with no scheduled reset.
  EXPECT_TRUE(watchReturned);
  EXPECT_FALSE(state.has_scheduled_reset_time());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;

  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kButtonCountdownDuration);
  EXPECT_EQ(FactoryResetState::RESET_COUNTDOWN, factory_reset_manager_->factory_reset_state());

  fuchsia::recovery::ui::FactoryResetCountdownState secondState;
  bool secondWatchReturned = false;

  auto secondWatchCallback = [&secondState, &secondWatchReturned](
                                 fuchsia::recovery::ui::FactoryResetCountdownState newState) {
    secondWatchReturned = true;
    newState.Clone(&secondState);
  };
  countdown_ptr_->Watch(secondWatchCallback);

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_->factory_reset_state());

  // Countdown should be sent.
  EXPECT_TRUE(secondWatchReturned);
  EXPECT_TRUE(secondState.has_scheduled_reset_time());
}

TEST_F(FactoryResetManagerTest, DoNotBroadcastCountdownWhenDisallowed) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());

  fuchsia::recovery::ui::FactoryResetCountdownState state;

  policy_ptr_->SetIsLocalResetAllowed(false);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  bool watchReturned = false;

  auto watchCallback =
      [&state, &watchReturned](fuchsia::recovery::ui::FactoryResetCountdownState newState) {
        watchReturned = true;
        newState.Clone(&state);
      };

  countdown_ptr_->Watch(watchCallback);
  RunLoopUntilIdle();

  // Initial watch should return immediately, with no scheduled reset.
  EXPECT_TRUE(watchReturned);
  EXPECT_FALSE(state.has_scheduled_reset_time());

  bool secondWatchReturned = false;

  auto secondWatchCallback =
      [&secondWatchReturned](fuchsia::recovery::ui::FactoryResetCountdownState newState) {
        secondWatchReturned = true;
      };
  countdown_ptr_->Watch(secondWatchCallback);

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;

  EXPECT_TRUE(factory_reset_manager_->OnMediaButtonReport(report));

  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kButtonCountdownDuration);
  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  RunLoopFor(kResetCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  // Countdown should not be be sent if the policy is disallowed.
  EXPECT_FALSE(secondWatchReturned);
}

TEST_F(FactoryResetManagerTest, WatchHandler) {
  const int scheduled_reset_time = 200;

  fuchsia::recovery::ui::FactoryResetCountdownState inputState;
  fuchsia::recovery::ui::FactoryResetCountdownState outputState;

  FactoryResetManager::WatchHandler watchHandler(inputState);

  bool watchReturned = false;

  auto watchCallback =
      [&outputState, &watchReturned](fuchsia::recovery::ui::FactoryResetCountdownState newState) {
        watchReturned = true;
        newState.Clone(&outputState);
      };

  watchHandler.Watch(watchCallback);

  // Initial watch should return immediately, with no scheduled reset.
  EXPECT_TRUE(watchReturned);
  EXPECT_FALSE(outputState.has_scheduled_reset_time());
  watchReturned = false;

  watchHandler.Watch(watchCallback);

  // Subsequent watch should hang until state changes.
  EXPECT_FALSE(watchReturned);

  inputState.set_scheduled_reset_time(scheduled_reset_time);
  watchHandler.OnStateChange(inputState);

  // On the state change, the watch should return with the new scheduled reset time.
  EXPECT_TRUE(watchReturned);
  EXPECT_TRUE(outputState.has_scheduled_reset_time());
  EXPECT_EQ(outputState.scheduled_reset_time(), scheduled_reset_time);
  watchReturned = false;

  inputState.clear_scheduled_reset_time();
  watchHandler.OnStateChange(inputState);

  watchHandler.Watch(watchCallback);

  // When state changes before watch is called, watch should return immediately.
  EXPECT_TRUE(watchReturned);
  EXPECT_FALSE(outputState.has_scheduled_reset_time());
}

class MockMediaRetriever : public MediaRetriever {
 public:
  virtual ~MockMediaRetriever() {}

  MOCK_METHOD(MediaRetriever::ResetSoundResult, GetResetSound, ());
};

class FakeSoundPlayer : public Player_TestBase {
 public:
  FakeSoundPlayer(MockFunction<void(std::string check_point_name)>* check) : check_(check) {}
  virtual ~FakeSoundPlayer() {}

  void NotImplemented_(const std::string& name) final {}

  fidl::InterfaceRequestHandler<fuchsia::media::sounds::Player> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::media::sounds::Player> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  void AddSoundFromFile(uint32_t id, ::fidl::InterfaceHandle<class ::fuchsia::io::File> handle,
                        Player_TestBase::AddSoundFromFileCallback cb) override {
    ASSERT_EQ(id, static_cast<uint32_t>(0));
    check_->Call("AddSoundFromFile");
    if (add) {
      cb(Player_AddSoundFromFile_Result::WithResponse(Player_AddSoundFromFile_Response(10)));
    } else {
      cb(Player_AddSoundFromFile_Result::WithErr(10));
    }
  }
  void PlaySound(uint32_t id, AudioRenderUsage usage,
                 Player_TestBase::PlaySoundCallback cb) override {
    ASSERT_EQ(id, static_cast<uint32_t>(0));
    check_->Call("PlaySound");
    if (play) {
      cb(Player_PlaySound_Result::WithResponse(Player_PlaySound_Response()));
    } else {
      cb(Player_PlaySound_Result::WithErr(PlaySoundError::NO_SUCH_SOUND));
    }
  }
  void RemoveSound(uint32_t id) override {
    ASSERT_EQ(id, static_cast<uint32_t>(0));
    check_->Call("RemoveSound");
  }

  bool add = true;
  bool play = true;

 private:
  MockFunction<void(std::string check_point_name)>* check_ = nullptr;
  fidl::BindingSet<fuchsia::media::sounds::Player> bindings_;
};

class FactoryResetManagerSoundTest : public gtest::TestLoopFixture {
 public:
  FactoryResetManagerSoundTest()
      : media_retriever_(std::make_shared<NiceMock<MockMediaRetriever>>()),
        factory_reset_manager_(*context_provider_.context(), media_retriever_),
        factory_reset_(&check_),
        sound_player_(&check_) {
    context_provider_.service_directory_provider()->AddService(factory_reset_.GetHandler());
    context_provider_.service_directory_provider()->AddService(sound_player_.GetHandler());
  }

  void TriggerFactoryReset() {
    // Send a media buttons report with the FDR button set to true.
    fuchsia::ui::input::MediaButtonsReport report;
    report.reset = true;
    EXPECT_TRUE(factory_reset_manager_.OnMediaButtonReport(report));

    // Run a loop for the duration of the countdown delay.
    EXPECT_EQ(FactoryResetState::BUTTON_COUNTDOWN, factory_reset_manager_.factory_reset_state());
    RunLoopFor(kButtonCountdownDuration);

    // Run a loop for the duration of the factory reset countdown.
    EXPECT_EQ(FactoryResetState::RESET_COUNTDOWN, factory_reset_manager_.factory_reset_state());
    RunLoopFor(kResetCountdownDuration);
    RunLoopUntilIdle();
  }

  bool triggered() const { return factory_reset_.triggered(); }

  void SetUp() override {
    zx::channel client;
    zx::channel::create(0, &client, &server_);
    EXPECT_CALL(*media_retriever_, GetResetSound())
        .WillRepeatedly(
            Return(ByMove(fit::ok(fidl::InterfaceHandle<fuchsia::io::File>(std::move(client))))));

    sound_player_.add = true;
    sound_player_.play = true;
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::shared_ptr<NiceMock<MockMediaRetriever>> media_retriever_;
  FactoryResetManager factory_reset_manager_;
  FakeFactoryReset factory_reset_;
  FakeSoundPlayer sound_player_;
  MockFunction<void(std::string check_point_name)> check_;
  zx::channel server_;
};

TEST_F(FactoryResetManagerSoundTest, FactoryResetManagerPlaysSoundBeforeReset) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_.factory_reset_state());

  {
    InSequence s;

    EXPECT_CALL(check_, Call("AddSoundFromFile"));
    EXPECT_CALL(check_, Call("PlaySound"));
    EXPECT_CALL(check_, Call("RemoveSound"));
    EXPECT_CALL(check_, Call("Reset"));
  }

  TriggerFactoryReset();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_.factory_reset_state());
}

TEST_F(FactoryResetManagerSoundTest, FactoryResetManagerResetsWhenFailsToGetSound) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_.factory_reset_state());

  EXPECT_CALL(*media_retriever_, GetResetSound()).WillOnce(Return(ByMove(fit::error(-1))));
  EXPECT_CALL(check_, Call("Reset")).Times(1);
  EXPECT_CALL(check_, Call("AddSoundFromFile")).Times(0);
  EXPECT_CALL(check_, Call("PlaySound")).Times(0);
  EXPECT_CALL(check_, Call("RemoveSound")).Times(0);

  TriggerFactoryReset();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_.factory_reset_state());
}

TEST_F(FactoryResetManagerSoundTest, FactoryResetManagerResetsWhenFailsToAddSound) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_.factory_reset_state());

  sound_player_.add = false;

  {
    InSequence s;

    EXPECT_CALL(check_, Call("AddSoundFromFile")).Times(1);
    EXPECT_CALL(check_, Call("Reset")).Times(1);
  }
  EXPECT_CALL(check_, Call("PlaySound")).Times(0);
  EXPECT_CALL(check_, Call("RemoveSound")).Times(0);

  TriggerFactoryReset();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_.factory_reset_state());
}

TEST_F(FactoryResetManagerSoundTest, FactoryResetManagerResetsWhenFailsToPlaySound) {
  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_.factory_reset_state());

  sound_player_.play = false;

  {
    InSequence s;

    EXPECT_CALL(check_, Call("AddSoundFromFile")).Times(1);
    EXPECT_CALL(check_, Call("PlaySound")).Times(1);
    EXPECT_CALL(check_, Call("Reset")).Times(1);
  }
  EXPECT_CALL(check_, Call("RemoveSound")).Times(0);

  TriggerFactoryReset();
  EXPECT_TRUE(triggered());
  EXPECT_EQ(FactoryResetState::TRIGGER_RESET, factory_reset_manager_.factory_reset_state());
}

TEST_F(FactoryResetManagerTestWithResetInitiallyDisallowed, FactoryResetInitiallyDisallowed) {
  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());
}

TEST_F(FactoryResetManagerTestWithResetInitiallyDisallowed,
       FactoryResetInitiallyDisallowedThenEnabled) {
  EXPECT_EQ(FactoryResetState::DISALLOWED, factory_reset_manager_->factory_reset_state());

  policy_ptr_->SetIsLocalResetAllowed(true);
  RunLoopUntilIdle();

  EXPECT_EQ(FactoryResetState::ALLOWED, factory_reset_manager_->factory_reset_state());
}

}  // namespace testing
}  // namespace root_presenter
