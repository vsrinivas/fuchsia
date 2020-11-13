// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"

#include <optional>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/fakes/privacy_settings.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace crash_reports {
namespace {

constexpr auto kDefaultPolicy = ReportingPolicy::kUndecided;

class ReportingPolicyWatcherTest : public testing::Test, public ReportingPolicyWatcher {
 public:
  ReportingPolicyWatcherTest() : ReportingPolicyWatcher(kDefaultPolicy) {}
};

TEST_F(ReportingPolicyWatcherTest, SetPolicy) {
  std::optional<ReportingPolicy> policy{std::nullopt};
  OnPolicyChange([&](const ReportingPolicy new_policy) { policy = new_policy; });

  SetPolicy(ReportingPolicy::kDoNotFileAndDelete);
  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kDoNotFileAndDelete);

  SetPolicy(ReportingPolicy::kUpload);
  EXPECT_EQ(policy.value(), ReportingPolicy::kUpload);

  SetPolicy(ReportingPolicy::kArchive);
  EXPECT_EQ(policy.value(), ReportingPolicy::kArchive);

  SetPolicy(ReportingPolicy::kUndecided);
  EXPECT_EQ(policy.value(), ReportingPolicy::kUndecided);
}

TEST_F(ReportingPolicyWatcherTest, SetPolicy_PoliciesAreIdentical) {
  bool called{false};
  OnPolicyChange([&](const ReportingPolicy new_policy) { called = true; });

  SetPolicy(kDefaultPolicy);
  ASSERT_FALSE(called);
}

constexpr bool kUserOptIn = true;
constexpr bool kUserOptOut = false;
constexpr std::optional<bool> kNotSet = std::nullopt;

class UserReportingPolicyWatcherTest : public UnitTestFixture, public UserReportingPolicyWatcher {
 protected:
  UserReportingPolicyWatcherTest() : UserReportingPolicyWatcher(dispatcher(), services()) {}

  void SetUpPrivacySettingsServer(std::unique_ptr<fakes::PrivacySettings> server) {
    privacy_settings_server_ = std::move(server);
    if (privacy_settings_server_) {
      InjectServiceProvider(privacy_settings_server_.get());
    }
  }

  void SetPrivacySettings(std::optional<bool> user_data_sharing_consent) {
    ::fit::result<void, fuchsia::settings::Error> set_result;
    fuchsia::settings::PrivacySettings settings;
    if (user_data_sharing_consent.has_value()) {
      settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
    }

    privacy_settings_server_->Set(
        std::move(settings), [&set_result](::fit::result<void, fuchsia::settings::Error> result) {
          set_result = std::move(result);
        });
    FX_CHECK(set_result.is_ok());
  }

  void CloseConnection() { privacy_settings_server_->CloseConnection(); }

 private:
  std::unique_ptr<fakes::PrivacySettings> privacy_settings_server_;
};

TEST_F(UserReportingPolicyWatcherTest, DefaultsToUndecided) {
  EXPECT_EQ(CurrentPolicy(), ReportingPolicy::kUndecided);
}

TEST_F(UserReportingPolicyWatcherTest, UserDataSharingConsentAffirmative) {
  std::optional<ReportingPolicy> policy{std::nullopt};
  OnPolicyChange([&](const ReportingPolicy new_policy) { policy = new_policy; });

  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());

  SetPrivacySettings(kUserOptIn);
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kUpload);
}

TEST_F(UserReportingPolicyWatcherTest, UserDataSharingConsentNegative) {
  std::optional<ReportingPolicy> policy{std::nullopt};
  OnPolicyChange([&](const ReportingPolicy new_policy) { policy = new_policy; });

  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());

  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kDoNotFileAndDelete);
}

TEST_F(UserReportingPolicyWatcherTest, UserDataSharingConsentUnknown) {
  std::optional<ReportingPolicy> policy{std::nullopt};
  OnPolicyChange([&](const ReportingPolicy new_policy) { policy = new_policy; });

  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());

  // First opt out so SetPolicy() will execute the callback when the user consent becomes unknown.
  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kDoNotFileAndDelete);

  SetPrivacySettings(kNotSet);
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kUndecided);
}

TEST_F(UserReportingPolicyWatcherTest, ReportingPolicyBecomesUnknowOnLostConnection) {
  std::optional<ReportingPolicy> policy{std::nullopt};
  OnPolicyChange([&](const ReportingPolicy new_policy) { policy = new_policy; });

  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());

  // First opt out so SetPolicy() will execute the callback when the user consent becomes unknown.
  SetPrivacySettings(kUserOptOut);
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kDoNotFileAndDelete);

  CloseConnection();
  RunLoopUntilIdle();

  ASSERT_TRUE(policy.has_value());
  EXPECT_EQ(policy.value(), ReportingPolicy::kUndecided);
}

}  // namespace

// Pretty-prints ReportingPolicy in gTest matchers instead of the default byte
// string in case of failed expectations.
void PrintTo(const ReportingPolicy& policy, std::ostream* os) { *os << ToString(policy); }

}  // namespace crash_reports
}  // namespace forensics
