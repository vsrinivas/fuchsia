// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager unit tests using DEV auth provider.

#include <memory>
#include <string>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/auth/store/auth_db.h"
#include "garnet/bin/auth/store/auth_db_file_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_factory_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_impl.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/callback/capture.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/svc/cpp/services.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

namespace e2e_dev {
namespace {

const std::string kTestUserId = "tq_auth_user_1";
const std::string kTestUserProfileId = "tq_auth_user_profile_1";
const std::string kTestAppUrl = "/pkgfs/packages/dev_auth_provider/bin/app";
const std::string kDevIdp = "Dev";

fuchsia::auth::AppConfig MakeDevAppConfig() {
  fuchsia::auth::AppConfig dev_app_config;
  dev_app_config.auth_provider_type = kDevIdp;
  dev_app_config.client_id = "test_client_id";
  dev_app_config.client_secret = "test_client_secret";
  return dev_app_config;
}

using fuchsia::auth::AppConfig;
using fuchsia::auth::Status;

class DevTokenManagerAppTest : public gtest::RealLoopFixture,
                               fuchsia::auth::AuthenticationContextProvider {
 public:
  DevTokenManagerAppTest()
      : startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
        auth_context_provider_binding_(this) {}

  ~DevTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    fuchsia::sys::Services services;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "token_manager";
    launch_info.directory_request = services.NewRequest();
    {
      std::ostringstream stream;
      stream << "--verbose=" << fxl::GetVlogVerbosity();
      launch_info.arguments.push_back(stream.str());
    }
    startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                  controller_.NewRequest());
    controller_.set_error_handler([] {
      FXL_LOG(ERROR) << "Error in connecting to TokenManagerFactory service.";
    });

    services.ConnectToService(token_mgr_factory_.NewRequest());

    fuchsia::auth::AuthProviderConfig dev_config;
    dev_config.auth_provider_type = kDevIdp;
    dev_config.url =
        "/pkgfs/packages/token_manager_tests/0/bin/dev_auth_provider_rust";

    fidl::VectorPtr<fuchsia::auth::AuthProviderConfig> auth_provider_configs;
    auth_provider_configs.push_back(std::move(dev_config));

    token_mgr_factory_->GetTokenManager(
        kTestUserId, kTestAppUrl, std::move(auth_provider_configs),
        auth_context_provider_binding_.NewBinding(), token_mgr_.NewRequest());

    // Make sure the state is clean
    // TODO: Once namespace for file system is per user, this won't be needed
    Status status;
    token_mgr_->DeleteAllTokens(MakeDevAppConfig(), kTestUserProfileId,
                                &status);
    ASSERT_EQ(Status::OK, status);
  }

  // |AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override {
    FXL_LOG(INFO) << "DevTokenManagerAppTest::GetAuthenticationUIContext() is "
                     "unimplemented.";
  }

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fuchsia::sys::ComponentControllerPtr controller_;

 protected:
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      auth_context_provider_binding_;

  fuchsia::auth::TokenManagerSyncPtr token_mgr_;
  fuchsia::auth::TokenManagerFactorySyncPtr token_mgr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevTokenManagerAppTest);
};

TEST_F(DevTokenManagerAppTest, Authorize) {
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  scopes.push_back("test_scope");

  Status status;
  fuchsia::auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(MakeDevAppConfig(), std::move(scopes), "", &status,
                        &user_info);
  ASSERT_EQ(Status::OK, status);
  ASSERT_FALSE(!user_info);
  EXPECT_FALSE(user_info->id.get().empty());
  EXPECT_FALSE(user_info->display_name.get().empty());
  EXPECT_FALSE(user_info->url.get().empty());
  EXPECT_FALSE(user_info->image_url.get().empty());
}

TEST_F(DevTokenManagerAppTest, GetAccessToken) {
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  Status status;
  fidl::StringPtr access_token;

  token_mgr_->GetAccessToken(MakeDevAppConfig(), kTestUserProfileId,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetIdToken) {
  Status status;
  fidl::StringPtr id_token;

  token_mgr_->GetIdToken(MakeDevAppConfig(), kTestUserProfileId, "", &status,
                         &id_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetFirebaseToken) {
  Status status;
  fuchsia::auth::FirebaseTokenPtr firebase_token;

  token_mgr_->GetFirebaseToken(MakeDevAppConfig(), kTestUserProfileId,
                               "firebase_test_api_key", "", &status,
                               &firebase_token);
  ASSERT_EQ(Status::OK, status);
  if (firebase_token) {
    EXPECT_TRUE(firebase_token->id_token.get().find(":fbt_") !=
                std::string::npos);
    EXPECT_TRUE(firebase_token->email.get().find("@firebase.example.com") !=
                std::string::npos);
    EXPECT_TRUE(firebase_token->local_id.get().find("local_id_") !=
                std::string::npos);
  }
}

TEST_F(DevTokenManagerAppTest, GetCachedFirebaseToken) {
  Status status;
  fuchsia::auth::FirebaseTokenPtr firebase_token;
  fuchsia::auth::FirebaseTokenPtr other_firebase_token;
  fuchsia::auth::FirebaseTokenPtr cached_firebase_token;

  token_mgr_->GetFirebaseToken(MakeDevAppConfig(), kTestUserProfileId, "",
                               "key1", &status, &firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(MakeDevAppConfig(), kTestUserProfileId, "",
                               "key2", &status, &other_firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(MakeDevAppConfig(), kTestUserProfileId, "",
                               "key1", &status, &cached_firebase_token);
  ASSERT_EQ(Status::OK, status);

  ASSERT_NE(firebase_token->id_token, other_firebase_token->id_token);
  ASSERT_EQ(firebase_token->id_token, cached_firebase_token->id_token);
  ASSERT_EQ(firebase_token->email, cached_firebase_token->email);
  ASSERT_EQ(firebase_token->local_id, cached_firebase_token->local_id);
}

TEST_F(DevTokenManagerAppTest, EraseAllTokens) {
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  Status status;

  fidl::StringPtr old_id_token;
  fidl::StringPtr old_access_token;
  fidl::StringPtr new_id_token;
  fidl::StringPtr new_access_token;
  fuchsia::auth::FirebaseTokenPtr old_firebase_token;
  fuchsia::auth::FirebaseTokenPtr new_firebase_token;

  auto dev_app_config = MakeDevAppConfig();

  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &old_id_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetAccessToken(dev_app_config, kTestUserProfileId,
                             std::move(scopes), &status, &old_access_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(dev_app_config, kTestUserProfileId, "", "",
                               &status, &old_firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->DeleteAllTokens(dev_app_config, kTestUserProfileId, &status);
  ASSERT_EQ(Status::OK, status);

  scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &new_id_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetAccessToken(dev_app_config, kTestUserProfileId,
                             std::move(scopes), &status, &new_access_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(dev_app_config, kTestUserProfileId, "", "",
                               &status, &new_firebase_token);
  ASSERT_EQ(Status::OK, status);

  ASSERT_NE(old_id_token, new_id_token);
  ASSERT_NE(old_access_token, new_access_token);
  ASSERT_NE(old_firebase_token->id_token, new_firebase_token->id_token);
}

TEST_F(DevTokenManagerAppTest, GetIdTokenFromCache) {
  Status status;
  fidl::StringPtr id_token;
  fidl::StringPtr cached_id_token;

  auto dev_app_config = MakeDevAppConfig();

  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &id_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &cached_id_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  ASSERT_EQ(id_token.get(), cached_id_token.get());

  token_mgr_->DeleteAllTokens(dev_app_config, kTestUserProfileId, &status);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &cached_id_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  ASSERT_NE(id_token.get(), cached_id_token.get());
}

TEST_F(DevTokenManagerAppTest, GetAccessTokenFromCache) {
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  Status status;
  fidl::StringPtr id_token;
  fidl::StringPtr access_token;
  fidl::StringPtr cached_access_token;
  auto dev_app_config = MakeDevAppConfig();

  token_mgr_->GetAccessToken(dev_app_config, kTestUserProfileId,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetIdToken(dev_app_config, kTestUserProfileId, "", &status,
                         &id_token);
  ASSERT_EQ(Status::OK, status);

  scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);
  token_mgr_->GetAccessToken(dev_app_config, kTestUserProfileId,
                             std::move(scopes), &status, &cached_access_token);
  ASSERT_EQ(Status::OK, status);

  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
  ASSERT_EQ(access_token.get(), cached_access_token.get());
}

TEST_F(DevTokenManagerAppTest, GetAndRevokeCredential) {
  std::string credential;
  fidl::StringPtr token;
  auto scopes = fidl::VectorPtr<fidl::StringPtr>::New(0);

  Status status;
  fuchsia::auth::UserProfileInfoPtr user_info;

  auto dev_app_config = MakeDevAppConfig();

  token_mgr_->Authorize(dev_app_config, std::move(scopes), "", &status,
                        &user_info);

  ASSERT_EQ(Status::OK, status);

  fidl::StringPtr user_profile_id = user_info->id;

  // Obtain the stored credential
  auth::store::AuthDbFileImpl auth_db(auth::kAuthDbPath + kTestUserId +
                                      auth::kAuthDbPostfix);
  auto db_status = auth_db.Load();
  EXPECT_EQ(db_status, auth::store::Status::kOK);
  db_status = auth_db.GetRefreshToken(
      auth::store::CredentialIdentifier(user_profile_id, kDevIdp), &credential);
  EXPECT_EQ(db_status, auth::store::Status::kOK);

  EXPECT_TRUE(credential.find("rt_") != std::string::npos);

  token_mgr_->GetIdToken(dev_app_config, user_profile_id, "", &status, &token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) != std::string::npos);

  token_mgr_->GetAccessToken(dev_app_config, user_profile_id, std::move(scopes),
                             &status, &token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) != std::string::npos);

  token_mgr_->DeleteAllTokens(dev_app_config, user_profile_id, &status);
  ASSERT_EQ(Status::OK, status);

  // The credential should now be revoked
  token_mgr_->GetIdToken(dev_app_config, user_profile_id, "", &status, &token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) == std::string::npos);

  token_mgr_->GetAccessToken(dev_app_config, user_profile_id, std::move(scopes),
                             &status, &token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(token.get().find(credential) == std::string::npos);
}

}  // namespace
}  // namespace e2e_dev

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  {
    async::Loop loop(&kAsyncLoopConfigMakeDefault);
    auto context =
        fuchsia::sys::StartupContext::CreateFromStartupInfoNotChecked();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
