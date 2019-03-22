// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager unit tests using DEV auth provider.

#include <memory>
#include <string>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/sys/cpp/service_directory.h"
#include "lib/sys/cpp/component_context.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

namespace e2e_dev {
namespace {

struct TestAuthProviderParams {
  const std::string type;
  const char* url;
};

struct TestComponentParam {
  const TestAuthProviderParams auth_provider_params;
  const char* token_manager_url;
};

const std::string kTestUserId = "tq_auth_user_1";
const std::string kTestAppUrl = "/pkgfs/packages/test_auth_client/bin/app";
const std::string kDevIdp = "Dev";
const std::string kDevIotIDIdp = "DevIotID";

const TestComponentParam kTestComponentParams[] = {
    {{kDevIdp,
      "fuchsia-pkg://fuchsia.com/token_manager_integration_tests#"
      "meta/dev_auth_provider.cmx"},
     "fuchsia-pkg://fuchsia.com/token_manager_factory#"
     "meta/token_manager_factory.cmx"},
    {{kDevIotIDIdp,
      "fuchsia-pkg://fuchsia.com/token_manager_integration_tests#"
      "meta/dev_auth_provider_iotid.cmx"},
     "fuchsia-pkg://fuchsia.com/token_manager_factory#"
     "meta/token_manager_factory.cmx"}};

fuchsia::auth::AppConfig MakeDevAppConfig(
    const std::string& auth_provider_type) {
  fuchsia::auth::AppConfig dev_app_config;
  dev_app_config.auth_provider_type = auth_provider_type;
  dev_app_config.client_id = "test_client_id";
  dev_app_config.client_secret = "test_client_secret";
  return dev_app_config;
}

using fuchsia::auth::AppConfig;
using fuchsia::auth::Status;

class DevTokenManagerAppTest
    : public gtest::RealLoopFixture,
      public ::testing::WithParamInterface<TestComponentParam>,
      fuchsia::auth::AuthenticationContextProvider {
 public:
  DevTokenManagerAppTest()
      : startup_context_(sys::ComponentContext::Create()),
        auth_context_provider_binding_(this) {}

  ~DevTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    fidl::InterfaceHandle<fuchsia::io::Directory> services;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = GetParam().token_manager_url;
    launch_info.directory_request = services.NewRequest().TakeChannel();
    {
      std::ostringstream stream;
      stream << "--verbose=" << fxl::GetVlogVerbosity();
      launch_info.arguments.push_back(stream.str());
    }
    fuchsia::sys::LauncherPtr launcher;
    startup_context_->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
    controller_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Error in connecting to TokenManagerFactory service.";
    });

    sys::ServiceDirectory service_directory(std::move(services));
    service_directory.Connect(token_mgr_factory_.NewRequest());

    std::string auth_provider_type = GetParam().auth_provider_params.type;
    dev_app_config_ = MakeDevAppConfig(auth_provider_type);

    fuchsia::auth::AuthProviderConfig dev_auth_provider_config;
    dev_auth_provider_config.auth_provider_type = auth_provider_type;
    dev_auth_provider_config.url = GetParam().auth_provider_params.url;
    std::vector<fuchsia::auth::AuthProviderConfig> auth_provider_configs;
    auth_provider_configs.push_back(std::move(dev_auth_provider_config));

    token_mgr_factory_->GetTokenManager(
        kTestUserId, kTestAppUrl, std::move(auth_provider_configs),
        auth_context_provider_binding_.NewBinding(), token_mgr_.NewRequest());
  }

  // ::testing::Test:
  void TearDown() override {
    // We attempt to clean up the tokens after each test. The Auth Provider
    // uses a different user_profile_id for each test and so any problems with
    // deletion should not impact test accuracy.
    if (token_mgr_.is_bound() && !user_profile_id_.is_null()) {
      Status status;
      token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id_, &status);
    }
  }

  // |AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override {
    FXL_LOG(INFO) << "DevTokenManagerAppTest::GetAuthenticationUIContext() is "
                     "unimplemented.";
  }

 private:
  std::unique_ptr<sys::ComponentContext> startup_context_;
  fuchsia::sys::ComponentControllerPtr controller_;

 protected:
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      auth_context_provider_binding_;

  fuchsia::auth::AppConfig dev_app_config_;
  fuchsia::auth::TokenManagerSyncPtr token_mgr_;
  fuchsia::auth::TokenManagerFactorySyncPtr token_mgr_factory_;
  fidl::StringPtr user_profile_id_;

  void RegisterUser(fuchsia::auth::AppConfig app_config) {
    auto scopes = fidl::VectorPtr<std::string>::New(0);
    scopes.push_back("test_scope");

    Status status;
    fuchsia::auth::UserProfileInfoPtr user_info;

    token_mgr_->Authorize(
        app_config, nullptr,   /* optional AuthenticationUiContext */
        std::move(scopes), "", /* new user, no existing user_profile_id */
        "",                    /* empty auth_code */
        &status, &user_info);
    ASSERT_EQ(Status::OK, status);
    ASSERT_FALSE(!user_info);
    user_profile_id_ = user_info->id;
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(DevTokenManagerAppTest);
};

TEST_P(DevTokenManagerAppTest, Authorize) {
  auto scopes = fidl::VectorPtr<std::string>::New(0);
  scopes.push_back("test_scope");

  Status status;
  fuchsia::auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(
      dev_app_config_, nullptr, /* optional AuthenticationUiContext */
      std::move(scopes), "",    /* new user, no existing user_profile_id */
      "",                       /* empty auth_code */
      &status, &user_info);
  ASSERT_EQ(Status::OK, status);
  ASSERT_FALSE(!user_info);
  EXPECT_FALSE(user_info->id.empty());
  EXPECT_FALSE(user_info->display_name.get().empty());
  EXPECT_FALSE(user_info->url.get().empty());
  EXPECT_FALSE(user_info->image_url.get().empty());
}

TEST_P(DevTokenManagerAppTest, GetAccessToken) {
  auto scopes = fidl::VectorPtr<std::string>::New(0);
  Status status;
  fidl::StringPtr access_token;

  RegisterUser(dev_app_config_);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
}

TEST_P(DevTokenManagerAppTest, GetIdToken) {
  Status status;
  fidl::StringPtr id_token;

  RegisterUser(dev_app_config_);
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &id_token);
  if (dev_app_config_.auth_provider_type == kDevIotIDIdp) {
    // TODO(ukode): Not yet supported for IotID
    ASSERT_EQ(Status::INVALID_REQUEST, status);
  } else {
    ASSERT_EQ(Status::OK, status);
    EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  }
}

TEST_P(DevTokenManagerAppTest, GetFirebaseToken) {
  Status status;
  fuchsia::auth::FirebaseTokenPtr firebase_token;

  RegisterUser(dev_app_config_);

  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_,
                               "firebase_test_api_key", "", &status,
                               &firebase_token);
  if (dev_app_config_.auth_provider_type == kDevIotIDIdp) {
    // TODO(ukode): Not yet supported for IotID
    ASSERT_EQ(Status::INVALID_REQUEST, status);
  } else {
    ASSERT_EQ(Status::OK, status);
    if (firebase_token) {
      EXPECT_TRUE(firebase_token->id_token.find(":fbt_") != std::string::npos);
      EXPECT_TRUE(firebase_token->email.get().find("@firebase.example.com") !=
                  std::string::npos);
      EXPECT_TRUE(firebase_token->local_id.get().find("local_id_") !=
                  std::string::npos);
    }
  }
}

TEST_P(DevTokenManagerAppTest, GetCachedFirebaseToken) {
  // TODO(ukode): Not yet supported for IotID
  if (dev_app_config_.auth_provider_type == kDevIotIDIdp) {
    return;
  }
  Status status;
  fuchsia::auth::FirebaseTokenPtr firebase_token;
  fuchsia::auth::FirebaseTokenPtr other_firebase_token;
  fuchsia::auth::FirebaseTokenPtr cached_firebase_token;

  RegisterUser(dev_app_config_);
  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_, "", "key1",
                               &status, &firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_, "", "key2",
                               &status, &other_firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_, "", "key1",
                               &status, &cached_firebase_token);
  ASSERT_EQ(Status::OK, status);

  ASSERT_NE(firebase_token->id_token, other_firebase_token->id_token);
  ASSERT_EQ(firebase_token->id_token, cached_firebase_token->id_token);
  ASSERT_EQ(firebase_token->email, cached_firebase_token->email);
  ASSERT_EQ(firebase_token->local_id, cached_firebase_token->local_id);
}

TEST_P(DevTokenManagerAppTest, EraseAllTokens) {
  // TODO(ukode): Not yet supported for IotID
  if (dev_app_config_.auth_provider_type == kDevIotIDIdp) {
    return;
  }
  auto scopes = fidl::VectorPtr<std::string>::New(0);
  Status status;

  fidl::StringPtr id_token;
  fidl::StringPtr access_token;
  fuchsia::auth::FirebaseTokenPtr firebase_token;

  RegisterUser(dev_app_config_);

  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &id_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_, "", "",
                               &status, &firebase_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id_, &status);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &id_token);
  ASSERT_EQ(Status::USER_NOT_FOUND, status);

  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::USER_NOT_FOUND, status);

  token_mgr_->GetFirebaseToken(dev_app_config_, user_profile_id_, "", "",
                               &status, &firebase_token);
  ASSERT_EQ(Status::USER_NOT_FOUND, status);
}

TEST_P(DevTokenManagerAppTest, GetIdTokenFromCache) {
  // TODO(ukode): Not yet supported for IotID
  if (dev_app_config_.auth_provider_type == kDevIotIDIdp) {
    return;
  }
  Status status;
  fidl::StringPtr id_token;
  fidl::StringPtr cached_id_token;
  fidl::StringPtr second_user_id_token;

  RegisterUser(dev_app_config_);

  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &id_token);
  ASSERT_EQ(Status::OK, status);

  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &cached_id_token);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
  ASSERT_EQ(id_token.get(), cached_id_token.get());

  // Verify ID tokens are different for different user to prevent a
  // degenerate test.
  fidl::StringPtr original_user_profile_id = user_profile_id_;
  RegisterUser(dev_app_config_);
  ASSERT_NE(user_profile_id_, original_user_profile_id);
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_, "", &status,
                         &second_user_id_token);
  ASSERT_NE(id_token.get(), second_user_id_token.get());
}

TEST_P(DevTokenManagerAppTest, GetAccessTokenFromCache) {
  auto scopes = fidl::VectorPtr<std::string>::New(0);
  Status status;
  fidl::StringPtr access_token;
  fidl::StringPtr cached_access_token;

  RegisterUser(dev_app_config_);

  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_,
                             std::move(scopes), &status, &access_token);
  ASSERT_EQ(Status::OK, status);

  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_,
                             std::move(scopes), &status, &cached_access_token);
  ASSERT_EQ(Status::OK, status);

  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
  ASSERT_EQ(access_token.get(), cached_access_token.get());
}

// Tests user re-authorization flow that generates fresh long lived credentials
// and verifies that short lived credentials are based on the most recent long
// lived credentials.
TEST_P(DevTokenManagerAppTest, Reauthorize) {
  fidl::StringPtr token;
  auto scopes = fidl::VectorPtr<std::string>::New(0);

  Status status;
  fuchsia::auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(dev_app_config_, nullptr, std::move(scopes), "", "",
                        &status, &user_info);
  ASSERT_EQ(Status::OK, status);
  fidl::StringPtr user_profile_id = user_info->id;

  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id,
                             std::move(scopes), &status, &token);
  ASSERT_EQ(Status::OK, status);
  std::string credential = token->substr(0, token->find(":"));

  token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id, &status);
  ASSERT_EQ(Status::OK, status);

  // Verify that the credential and cache should now be cleared
  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id,
                             std::move(scopes), &status, &token);
  ASSERT_EQ(Status::USER_NOT_FOUND, status);
  EXPECT_TRUE(token.get().empty());

  // Re-authorize and obtain a fresh credential for the same |user_profile_id|
  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->Authorize(dev_app_config_, nullptr, std::move(scopes),
                        user_profile_id, "", &status, &user_info);
  ASSERT_EQ(Status::OK, status);
  ASSERT_EQ(user_info->id, user_profile_id);

  // Verify that the new access token is not based on the old credential
  fidl::StringPtr token2;
  scopes = fidl::VectorPtr<std::string>::New(0);
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id,
                             std::move(scopes), &status, &token2);
  ASSERT_EQ(Status::OK, status);
  EXPECT_TRUE(token2.get().find(credential) == std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Cpp, DevTokenManagerAppTest,
                         ::testing::ValuesIn(kTestComponentParams));
}  // namespace
}  // namespace e2e_dev

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    auto context = sys::ComponentContext::Create();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
