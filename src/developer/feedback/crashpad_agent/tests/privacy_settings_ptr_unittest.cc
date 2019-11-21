// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/privacy_settings_ptr.h"

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/settings/formatting.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>

#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::settings::Error;
using fuchsia::settings::PrivacySettings;

constexpr Settings::UploadPolicy kDisabled = Settings::UploadPolicy::DISABLED;
constexpr Settings::UploadPolicy kEnabled = Settings::UploadPolicy::ENABLED;
constexpr Settings::UploadPolicy kLimbo = Settings::UploadPolicy::LIMBO;

constexpr bool kUserOptIn = true;
constexpr bool kUserOptOut = false;
constexpr std::optional<bool> kNotSet = std::nullopt;

PrivacySettings MakePrivacySettings(const std::optional<bool> user_data_sharing_consent) {
  PrivacySettings settings;
  if (user_data_sharing_consent.has_value()) {
    settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
  }
  return settings;
}

class PrivacySettingsWatcherTest : public UnitTestFixture,
                                   public testing::WithParamInterface<Settings::UploadPolicy> {
 public:
  PrivacySettingsWatcherTest() : watcher_(services(), &crash_reporter_settings_) {}

 protected:
  void SetUpPrivacySettingsProvider(
      std::unique_ptr<FakePrivacySettings> privacy_settings_provider) {
    privacy_settings_provider_ = std::move(privacy_settings_provider);
    if (privacy_settings_provider_) {
      InjectServiceProvider(privacy_settings_provider_.get());
    }
  }

  void SetPrivacySettings(std::optional<bool> user_data_sharing_consent) {
    fit::result<void, Error> set_result;
    privacy_settings_provider_->Set(
        MakePrivacySettings(user_data_sharing_consent),
        [&set_result](fit::result<void, Error> result) { set_result = std::move(result); });
    EXPECT_TRUE(set_result.is_ok());
  }

  void SetInitialUploadPolicy(const Settings::UploadPolicy upload_policy) {
    crash_reporter_settings_.set_upload_policy(upload_policy);
  }

 protected:
  Settings crash_reporter_settings_;
  PrivacySettingsWatcher watcher_;

 private:
  std::unique_ptr<FakePrivacySettings> privacy_settings_provider_;
};

TEST_F(PrivacySettingsWatcherTest, SetUp) {
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
  EXPECT_FALSE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
}

// This allows us to see meaningful names rather than /0, /1 and /2 in the parameterized test case
// names.
std::string PrettyPrintUploadPolicyUploadsEnabledValue(
    const testing::TestParamInfo<Settings::UploadPolicy>& info) {
  switch (info.param) {
    case Settings::UploadPolicy::DISABLED:
      return "DisabledInitially";
    case Settings::UploadPolicy::ENABLED:
      return "EnabledInitially";
    case Settings::UploadPolicy::LIMBO:
      return "LimboInitially";
  }
};

// We want to make sure that regardless of the state in which the crash reporter's upload policy
// started in, the expectations are always the same. In particular that failure paths always end up
// setting the upload policy to LIMBO.
//
// We use a parameterized gTest where the 3 values represent the 3 possible UploadPolicy.
INSTANTIATE_TEST_SUITE_P(WithVariousInitialUploadPolicies, PrivacySettingsWatcherTest,
                         ::testing::ValuesIn(std::vector<Settings::UploadPolicy>({
                             Settings::UploadPolicy::DISABLED,
                             Settings::UploadPolicy::ENABLED,
                             Settings::UploadPolicy::LIMBO,
                         })),
                         &PrettyPrintUploadPolicyUploadsEnabledValue);

TEST_P(PrivacySettingsWatcherTest, UploadPolicyDefaultToDisabledIfServerNotAvailable) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(nullptr);

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicyDefaultToDisabledIfServerClosesConnection) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettingsClosesConnection>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicyDefaultToDisabledIfNoCallToSet) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnFirstWatch_OptIn) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kUserOptIn);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnFirstWatch_OptOut) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kUserOptOut);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kDisabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnFirstWatch_NotSet) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  SetPrivacySettings(kNotSet);
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnSecondWatch_OptIn) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnSecondWatch_OptOut) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kDisabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnSecondWatch_NotSet) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

TEST_P(PrivacySettingsWatcherTest, UploadPolicySwitchesToSetValueOnEachWatch) {
  SetInitialUploadPolicy(GetParam());
  SetUpPrivacySettingsProvider(std::make_unique<FakePrivacySettings>());

  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kDisabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kDisabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kEnabled);
  EXPECT_TRUE(watcher_.privacy_settings().has_user_data_sharing_consent());

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();
  EXPECT_EQ(crash_reporter_settings_.upload_policy(), kLimbo);
  EXPECT_FALSE(watcher_.privacy_settings().has_user_data_sharing_consent());
}

}  // namespace

// Pretty-prints Settings::UploadPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const Settings::UploadPolicy& upload_policy, std::ostream* os) {
  *os << ToString(upload_policy);
}

}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
