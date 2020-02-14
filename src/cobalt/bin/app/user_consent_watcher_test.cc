// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/user_consent_watcher.h"

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include "sdk/lib/sys/cpp/testing/service_directory_provider.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

using fuchsia::settings::Error;
using fuchsia::settings::PrivacySettings;

PrivacySettings MakePrivacySettings(const std::optional<bool> user_data_sharing_consent) {
  PrivacySettings settings;
  if (user_data_sharing_consent.has_value()) {
    settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
  }
  return settings;
}

class FakePrivacy : public fuchsia::settings::Privacy {
 public:
  fidl::InterfaceRequestHandler<fuchsia::settings::Privacy> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::settings::Privacy> request) {
      first_call_ = true;
      binding_ =
          std::make_unique<fidl::Binding<fuchsia::settings::Privacy>>(this, std::move(request));
    };
  }

  void Watch(WatchCallback callback) override {
    if (!first_call_) {
      watchers_.push_back(std::move(callback));
      return;
    }

    fuchsia::settings::PrivacySettings settings;
    settings_.Clone(&settings);
    callback(fit::ok(std::move(settings)));
    first_call_ = false;
  }

  void Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) override {
    settings_ = std::move(settings);
    callback(fit::ok());

    NotifyWatchers();
  }

 protected:
  void CloseConnection() {
    if (binding_) {
      binding_->Unbind();
    }
  }

 private:
  void NotifyWatchers() {
    for (const auto& watcher : watchers_) {
      fuchsia::settings::PrivacySettings settings;
      settings_.Clone(&settings);
      watcher(fit::ok(std::move(settings)));
    }
    watchers_.clear();
  }

  std::unique_ptr<fidl::Binding<fuchsia::settings::Privacy>> binding_;
  fuchsia::settings::PrivacySettings settings_;
  bool first_call_ = true;
  std::vector<WatchCallback> watchers_;
};

class FakePrivacyClosesConnection : public FakePrivacy {
 public:
  void Watch(WatchCallback callback) { CloseConnection(); }
};

class FakePrivacyClosesConnectionOnce : public FakePrivacy {
 public:
  void Watch(WatchCallback callback) {
    if (!has_closed_once_) {
      has_closed_once_ = true;
      CloseConnection();
      return;
    }

    FakePrivacy::Watch(std::move(callback));
  }

 private:
  bool has_closed_once_ = false;
};

class UserConsentWatcherTest : public gtest::TestLoopFixture,
                               public testing::WithParamInterface<std::optional<bool>> {
 public:
  UserConsentWatcherTest()
      : gtest::TestLoopFixture(),
        service_directory_provider_(dispatcher()),
        watcher_(dispatcher(), service_directory_provider_.service_directory(),
                 [this](const CobaltService::DataCollectionPolicy& policy) { Callback(policy); }) {}

  void Callback(const CobaltService::DataCollectionPolicy& policy) { current_policy_ = policy; }

  void SetPrivacyProvider(std::unique_ptr<FakePrivacy> privacy_provider) {
    privacy_provider_ = std::move(privacy_provider);
    ASSERT_EQ(service_directory_provider_.AddService(privacy_provider_->GetHandler()), ZX_OK);
  }

  void CreatePrivacyProvider() { SetPrivacyProvider(std::make_unique<FakePrivacy>()); }

  void SetPrivacySetting(const std::optional<bool>& setting) {
    fit::result<void, Error> set_result;
    privacy_provider_->Set(
        MakePrivacySettings(setting),
        [&set_result](fit::result<void, Error> result) { set_result = std::move(result); });
    EXPECT_TRUE(set_result.is_ok());
  }

 protected:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  UserConsentWatcher watcher_;
  CobaltService::DataCollectionPolicy current_policy_;
  PrivacySettings last_settings_;
  std::unique_ptr<FakePrivacy> privacy_provider_;
};

std::string PrettyPrintConsentStates(const testing::TestParamInfo<std::optional<bool>>& info) {
  if (info.param.has_value()) {
    if (info.param.value()) {
      return "UserConsented";
    } else {
      return "UserDidNotConsent";
    }
  } else {
    return "NoConsentState";
  }
}

INSTANTIATE_TEST_SUITE_P(WithVariousConsentStates, UserConsentWatcherTest,
                         ::testing::Values(true, false, std::nullopt), &PrettyPrintConsentStates);

TEST_F(UserConsentWatcherTest, CallbackCalledIfServerNotAvailable) {
  watcher_.StartWatching();
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_.IsConnected());
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(UserConsentWatcherTest, ConsentResetIfServerClosesConnection) {
  SetPrivacyProvider(std::make_unique<FakePrivacyClosesConnection>());
  SetPrivacySetting(GetParam());

  watcher_.StartWatching();
  RunLoopUntilIdle();

  EXPECT_FALSE(watcher_.IsConnected());
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(UserConsentWatcherTest, WatcherReconnectsIfServerClosesConnection) {
  SetPrivacyProvider(std::make_unique<FakePrivacyClosesConnectionOnce>());
  SetPrivacySetting(GetParam());

  watcher_.StartWatching();
  RunLoopFor(zx::msec(200));

  EXPECT_TRUE(watcher_.IsConnected());
  if (GetParam().has_value()) {
    EXPECT_FALSE(watcher_.privacy_settings().IsEmpty());
    if (GetParam().value()) {
      EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD);
    } else {
      EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_COLLECT);
    }
  } else {
    EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
    EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);
  }
}

TEST_F(UserConsentWatcherTest, ConsentStateDefaultsToDoNotUpload) {
  CreatePrivacyProvider();

  watcher_.StartWatching();
  RunLoopUntilIdle();

  EXPECT_TRUE(watcher_.IsConnected());
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);
  EXPECT_TRUE(watcher_.privacy_settings().IsEmpty());
}

TEST_P(UserConsentWatcherTest, SettingWorks) {
  CreatePrivacyProvider();
  SetPrivacySetting(GetParam());

  watcher_.StartWatching();
  RunLoopUntilIdle();

  EXPECT_TRUE(watcher_.IsConnected());
  if (GetParam().has_value()) {
    EXPECT_FALSE(watcher_.privacy_settings().IsEmpty());
  }
}

TEST_P(UserConsentWatcherTest, ContinuesWatching) {
  CreatePrivacyProvider();
  SetPrivacySetting(GetParam());

  watcher_.StartWatching();
  RunLoopUntilIdle();

  EXPECT_TRUE(watcher_.IsConnected());
  if (GetParam().has_value()) {
    EXPECT_FALSE(watcher_.privacy_settings().IsEmpty());
    EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), GetParam().value());
  }

  SetPrivacySetting(false);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), false);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_COLLECT);

  SetPrivacySetting(true);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), true);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD);

  SetPrivacySetting(std::nullopt);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().has_user_data_sharing_consent(), false);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);

  SetPrivacySetting(GetParam());
  RunLoopUntilIdle();
  if (GetParam().has_value()) {
    EXPECT_FALSE(watcher_.privacy_settings().IsEmpty());
    EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), GetParam().value());
  }

  SetPrivacySetting(false);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), false);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_COLLECT);

  SetPrivacySetting(true);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), true);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::COLLECT_AND_UPLOAD);

  SetPrivacySetting(std::nullopt);
  RunLoopUntilIdle();
  EXPECT_EQ(watcher_.privacy_settings().has_user_data_sharing_consent(), false);
  EXPECT_EQ(current_policy_, CobaltService::DataCollectionPolicy::DO_NOT_UPLOAD);

  SetPrivacySetting(GetParam());
  RunLoopUntilIdle();
  if (GetParam().has_value()) {
    EXPECT_FALSE(watcher_.privacy_settings().IsEmpty());
    EXPECT_EQ(watcher_.privacy_settings().user_data_sharing_consent(), GetParam().value());
  }
}

}  // namespace cobalt
