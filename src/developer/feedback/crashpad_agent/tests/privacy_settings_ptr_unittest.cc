// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/privacy_settings_ptr.h"

#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "fuchsia/settings/cpp/fidl.h"
#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/crashpad/client/settings.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/file_path.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::settings::Privacy_Set_Result;
using fuchsia::settings::PrivacySettings;

constexpr bool kUserOptIn = true;
constexpr bool kUserOptOut = false;
constexpr std::optional<bool> kNotSet = std::nullopt;

PrivacySettings MakePrivacySettings(std::optional<bool> user_data_sharing_consent) {
  PrivacySettings settings;
  if (user_data_sharing_consent.has_value()) {
    settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
  }
  return settings;
}

class PrivacySettingsWatcherTest : public gtest::TestLoopFixture,
                                   public testing::WithParamInterface<std::optional<bool>> {
 public:
  PrivacySettingsWatcherTest()
      : service_directory_provider_(dispatcher()),
        watcher_(service_directory_provider_.service_directory(), &crashpad_settings_) {}

  void SetUp() override {
    crashpad_settings_filepath_ = files::JoinPath(tmp_dir_.path(), "settings.dat");
    ASSERT_TRUE(crashpad_settings_.Initialize(base::FilePath(crashpad_settings_filepath_)));
  }

 protected:
  void ResetPrivacySettings(std::unique_ptr<FakePrivacySettings> fake_privacy_settings) {
    fake_privacy_settings_ = std::move(fake_privacy_settings);
    if (fake_privacy_settings_) {
      FXL_CHECK(service_directory_provider_.AddService(fake_privacy_settings_->GetHandler()) ==
                ZX_OK);
    }
  }

  void SetPrivacySettings(std::optional<bool> user_data_sharing_consent) {
    Privacy_Set_Result set_result;
    fake_privacy_settings_->Set(
        MakePrivacySettings(user_data_sharing_consent),
        [&set_result](Privacy_Set_Result result) { set_result = std::move(result); });
    EXPECT_TRUE(set_result.is_response());
  }

  void SetCrashpadUploadsEnabled(std::optional<bool> enabled) {
    if (enabled.has_value()) {
      ASSERT_TRUE(crashpad_settings_.SetUploadsEnabled(enabled.value()));
    }
  }

  void ExpectCrashpadUploadsDisabled() {
    bool enabled;
    ASSERT_TRUE(crashpad_settings_.GetUploadsEnabled(&enabled));
    EXPECT_FALSE(enabled);
  }

  void ExpectCrashpadUploadsEnabled() {
    bool enabled;
    ASSERT_TRUE(crashpad_settings_.GetUploadsEnabled(&enabled));
    EXPECT_TRUE(enabled);
  }

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  crashpad::Settings crashpad_settings_;

 protected:
  PrivacySettingsWatcher watcher_;

 private:
  std::unique_ptr<FakePrivacySettings> fake_privacy_settings_;
  files::ScopedTempDir tmp_dir_;
  std::string crashpad_settings_filepath_;
};

TEST_F(PrivacySettingsWatcherTest, SetUp) {
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
  EXPECT_FALSE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
}

// This allows us to see meaningful names rather than /0, /1 and /2 in the parameterized test case
// names.
std::string PrettyPrintCrashpadUploadsEnabledValue(
    const testing::TestParamInfo<std::optional<bool>>& info) {
  if (!info.param.has_value()) {
    return "Uninitialized";
  }
  if (info.param.value()) {
    return "EnabledInitially";
  }
  return "DisabledInitially";
};

// We want to make sure that regardless of the state in which the Crashpad database's settings
// started in, the expectations are always the same. In particular that failure paths always end up
// setting "upload enable" to false in Crashpad. We use a parameterized gTest where the 3 values
// represent the 3 possible states in which "upload enable" can start in.
INSTANTIATE_TEST_SUITE_P(WithVariousInitialCrashpadUploadsEnabledValues, PrivacySettingsWatcherTest,
                         ::testing::ValuesIn(std::vector<std::optional<bool>>({
                             true,
                             false,
                             std::nullopt,
                         })),
                         &PrettyPrintCrashpadUploadsEnabledValue);

TEST_P(PrivacySettingsWatcherTest, CrashpadDefaultToDisabledIfServerNotAvailable) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(nullptr);

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  // Crashpad's database setting default to false so we cannot actually distinguish whether it was
  // not set or set to false. Thus, we also check the PrivacySettings object owned by the watcher.
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadDefaultToDisabledIfServerClosesConnection) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettingsClosesConnection>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadDefaultToDisabledIfNoCallToSet) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnFirstWatch_OptIn) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kUserOptIn);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnFirstWatch_OptOut) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kUserOptOut);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnFirstWatch_NotSet) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kNotSet);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnSecondWatch_OptIn) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnSecondWatch_OptOut) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnSecondWatch_NotSet) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, CrashpadSwitchesToSetValueOnEachWatch) {
  SetCrashpadUploadsEnabled(GetParam());
  ResetPrivacySettings(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsEnabled();
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  ExpectCrashpadUploadsDisabled();
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
