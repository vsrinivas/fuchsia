// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager integration tests using dev auth provider.

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace e2e_dev {
namespace {

struct TestComponentParam {
  const std::string auth_provider_type;
  const char* auth_provider_url;
};

const std::string kEnvironment = "dev_token_mgr_test_env";
const std::string kTestUserId = "tq_auth_user_1";
const std::string kTestAppUrl = "/pkgfs/packages/test_auth_client/bin/app";
const std::string kDevIdp = "Dev";
const bool kForce = true;

const std::string kTokenManagerFactoryUrl =
    "fuchsia-pkg://fuchsia.com/"
    "token_manager_factory#meta/token_manager_factory.cmx";
const std::string kDevAuthProviderUrl =
    "fuchsia-pkg://fuchsia.com/dev_auth_provider#"
    "meta/dev_auth_provider.cmx";

fuchsia::auth::AppConfig MakeDevAppConfig(const std::string& auth_provider_type) {
  fuchsia::auth::AppConfig dev_app_config;
  dev_app_config.auth_provider_type = auth_provider_type;
  dev_app_config.client_id = "test_client_id";
  dev_app_config.client_secret = "test_client_secret";
  return dev_app_config;
}

using fuchsia::auth::AppConfig;
using fuchsia::auth::Status;
using fuchsia::auth::TokenManagerFactory;
using fuchsia::auth::UserProfileInfoPtr;
using fuchsia::sys::LaunchInfo;
using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;

class DevTokenManagerAppTest : public sys::testing::TestWithEnvironment,
                               fuchsia::auth::AuthenticationContextProvider {
 public:
  DevTokenManagerAppTest() : auth_context_provider_binding_(this) {}

  ~DevTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    std::unique_ptr<EnvironmentServices> services = CreateServices();
    LaunchInfo launch_info;
    launch_info.url = kTokenManagerFactoryUrl;
    services->AddServiceWithLaunchInfo(std::move(launch_info), TokenManagerFactory::Name_);

    environment_ = CreateNewEnclosingEnvironment(kEnvironment, std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());

    environment_->ConnectToService(token_mgr_factory_.NewRequest());
    ASSERT_TRUE(token_mgr_factory_.is_bound());

    std::string auth_provider_type = kDevIdp;
    dev_app_config_ = MakeDevAppConfig(auth_provider_type);

    fuchsia::auth::AuthProviderConfig dev_auth_provider_config;
    dev_auth_provider_config.auth_provider_type = auth_provider_type;
    dev_auth_provider_config.url = kDevAuthProviderUrl;
    std::vector<fuchsia::auth::AuthProviderConfig> auth_provider_configs;
    auth_provider_configs.push_back(std::move(dev_auth_provider_config));

    token_mgr_factory_->GetTokenManager(kTestUserId, kTestAppUrl, std::move(auth_provider_configs),
                                        auth_context_provider_binding_.NewBinding(),
                                        token_mgr_.NewRequest());
  }

  // ::testing::Test:
  void TearDown() override {
    // We attempt to clean up the tokens after each test. The Auth Provider
    // uses a different user_profile_id for each test and so any problems with
    // deletion should not impact test accuracy.
    if (token_mgr_.is_bound() && user_profile_id_.has_value()) {
      bool call_complete = false;
      token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id_.value(), kForce,
                                  [&](Status status) { call_complete = true; });
      RunLoopUntil([&] { return call_complete; });
    }
  }

  // |AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) override {
    // Silently ignore this request. The TokenManager will pass the other end
    // of the channel into the dev auth provider, which will never attempt to
    // use it since it does not display UI.
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;

 protected:
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider> auth_context_provider_binding_;

  fuchsia::auth::AppConfig dev_app_config_;
  fuchsia::auth::TokenManagerPtr token_mgr_;
  fuchsia::auth::TokenManagerFactoryPtr token_mgr_factory_;
  fidl::StringPtr user_profile_id_;

  void RegisterUser(fuchsia::auth::AppConfig app_config) {
    std::vector<std::string> scopes;
    scopes.push_back("test_scope");

    bool call_complete = false;
    token_mgr_->Authorize(app_config, nullptr,        /* optional AuthenticationUiContext */
                          std::move(scopes), nullptr, /* new user, no existing user_profile_id */
                          "",                         /* empty auth_code */
                          [&](Status status, UserProfileInfoPtr user_info) {
                            EXPECT_EQ(Status::OK, status);
                            EXPECT_NE(nullptr, user_info);
                            user_profile_id_ = user_info->id;
                            call_complete = true;
                          });
    RunLoopUntil([&] { return call_complete; });
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(DevTokenManagerAppTest);
};

TEST_F(DevTokenManagerAppTest, Authorize) {
  std::vector<std::string> scopes;
  scopes.push_back("test_scope");
  bool call_complete = false;
  token_mgr_->Authorize(dev_app_config_, nullptr,   /* optional AuthenticationUiContext */
                        std::move(scopes), nullptr, /* new user, no existing user_profile_id */
                        "",                         /* empty auth_code */
                        [&](Status status, UserProfileInfoPtr user_info) {
                          EXPECT_EQ(Status::OK, status);
                          EXPECT_NE(nullptr, user_info);
                          EXPECT_FALSE(user_info->id.empty());
                          ASSERT_TRUE(user_info->display_name.has_value());
                          EXPECT_FALSE(user_info->display_name->empty());
                          ASSERT_FALSE(user_info->url.has_value());
                          ASSERT_TRUE(user_info->image_url.has_value());
                          EXPECT_FALSE(user_info->image_url->empty());
                          call_complete = true;
                        });

  RunLoopUntil([&] { return call_complete; });
}

TEST_F(DevTokenManagerAppTest, GetAccessToken) {
  RegisterUser(dev_app_config_);
  std::vector<std::string> scopes;
  bool call_complete = false;
  ASSERT_TRUE(user_profile_id_.has_value());
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_.value(), std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::OK, status);
                               ASSERT_TRUE(access_token.has_value());
                               EXPECT_NE(std::string::npos, access_token->find(":at_"));
                               call_complete = true;
                             });
  RunLoopUntil([&] { return call_complete; });
}

TEST_F(DevTokenManagerAppTest, GetIdToken) {
  RegisterUser(dev_app_config_);
  bool call_complete = false;
  ASSERT_TRUE(user_profile_id_.has_value());
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_.value(), nullptr,
                         [&](Status status, fidl::StringPtr id_token) {
                           EXPECT_EQ(Status::OK, status);
                           ASSERT_TRUE(id_token.has_value());
                           EXPECT_NE(std::string::npos, id_token->find(":idt_"));
                           call_complete = true;
                         });
  RunLoopUntil([&] { return call_complete; });
}

TEST_F(DevTokenManagerAppTest, EraseAllTokens) {
  RegisterUser(dev_app_config_);
  bool last_call_complete = false;

  ASSERT_TRUE(user_profile_id_.has_value());
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_.value(), nullptr,
                         [&](Status status, fidl::StringPtr id_token) {
                           EXPECT_EQ(Status::OK, status);
                           EXPECT_NE(nullptr, id_token);
                         });

  std::vector<std::string> scopes;
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_.value(), std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::OK, status);
                               EXPECT_TRUE(access_token.has_value());
                             });

  token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id_.value(), kForce,
                              [&](Status status) { EXPECT_EQ(Status::OK, status); });

  token_mgr_->GetIdToken(
      dev_app_config_, user_profile_id_.value(), nullptr,
      [&](Status status, fidl::StringPtr id_token) { EXPECT_EQ(Status::USER_NOT_FOUND, status); });

  scopes.clear();
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_.value(), std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::USER_NOT_FOUND, status);
                               last_call_complete = true;
                             });

  RunLoopUntil([&] { return last_call_complete; });
}

TEST_F(DevTokenManagerAppTest, GetIdTokenFromCache) {
  fidl::StringPtr id_token;
  fidl::StringPtr second_user_id_token;

  RegisterUser(dev_app_config_);

  bool last_call_complete = false;
  ASSERT_TRUE(user_profile_id_.has_value());
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_.value(), nullptr,
                         [&](Status status, fidl::StringPtr token) {
                           EXPECT_EQ(Status::OK, status);
                           id_token = std::move(token);
                         });

  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_.value(), nullptr,
                         [&](Status status, fidl::StringPtr token) {
                           EXPECT_EQ(Status::OK, status);
                           ASSERT_TRUE(id_token.has_value());
                           ASSERT_TRUE(token.has_value());
                           EXPECT_EQ(id_token.value(), token.value());
                         });

  // Verify ID tokens are different for different user to prevent a
  // degenerate test.
  fidl::StringPtr original_user_profile_id = user_profile_id_;
  RegisterUser(dev_app_config_);
  EXPECT_NE(user_profile_id_, original_user_profile_id);
  token_mgr_->GetIdToken(dev_app_config_, user_profile_id_.value(), nullptr,
                         [&](Status status, fidl::StringPtr token) {
                           EXPECT_EQ(Status::OK, status);
                           ASSERT_TRUE(id_token.has_value());
                           ASSERT_TRUE(token.has_value());
                           EXPECT_NE(id_token.value(), token.value());
                           last_call_complete = true;
                         });

  RunLoopUntil([&] { return last_call_complete; });
}

TEST_F(DevTokenManagerAppTest, GetAccessTokenFromCache) {
  fidl::StringPtr access_token;

  RegisterUser(dev_app_config_);

  bool last_call_complete = false;
  std::vector<std::string> scopes;
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_.value(), std::move(scopes),
                             [&](Status status, fidl::StringPtr token) {
                               EXPECT_EQ(Status::OK, status);
                               ASSERT_TRUE(token.has_value());
                               EXPECT_NE(std::string::npos, token->find(":at_"));
                               access_token = std::move(token);
                             });

  scopes.clear();
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id_.value(), std::move(scopes),
                             [&](Status status, fidl::StringPtr token) {
                               EXPECT_EQ(Status::OK, status);
                               ASSERT_TRUE(access_token.has_value());
                               ASSERT_TRUE(token.has_value());
                               EXPECT_EQ(access_token.value(), token.value());
                               last_call_complete = true;
                             });

  RunLoopUntil([&] { return last_call_complete; });
}

// Tests user re-authorization flow that generates fresh long lived credentials
// and verifies that short lived credentials are based on the most recent long
// lived credentials.
TEST_F(DevTokenManagerAppTest, Reauthorize) {
  std::string user_profile_id;
  std::string credential;
  bool authorize_complete = false;
  bool last_call_complete = false;

  std::vector<std::string> scopes;
  token_mgr_->Authorize(dev_app_config_, nullptr, std::move(scopes), nullptr, "",
                        [&](Status status, UserProfileInfoPtr user_info) {
                          EXPECT_EQ(Status::OK, status);
                          user_profile_id = user_info->id;
                          authorize_complete = true;
                        });
  RunLoopUntil([&] { return authorize_complete; });

  scopes.clear();
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id, std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::OK, status);
                               // Extract part of the fake access token reflecting the refresh token
                               credential = access_token->substr(0, access_token->find(":"));
                             });

  token_mgr_->DeleteAllTokens(dev_app_config_, user_profile_id, kForce,
                              [&](Status status) { EXPECT_EQ(Status::OK, status); });

  // Verify that the credential and cache should now be cleared
  scopes.clear();
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id, std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::USER_NOT_FOUND, status);
                               EXPECT_EQ(nullptr, access_token);
                             });

  // Re-authorize the same |user_profile_id|
  scopes.clear();
  token_mgr_->Authorize(dev_app_config_, nullptr, std::move(scopes), user_profile_id, "",
                        [&](Status status, UserProfileInfoPtr user_info) {
                          EXPECT_EQ(Status::OK, status);
                          EXPECT_EQ(user_info->id, user_profile_id);
                        });

  // Verify that new access token is not based on the original credential
  scopes.clear();
  token_mgr_->GetAccessToken(dev_app_config_, user_profile_id, std::move(scopes),
                             [&](Status status, fidl::StringPtr access_token) {
                               EXPECT_EQ(Status::OK, status);
                               EXPECT_NE(nullptr, access_token);
                               ASSERT_TRUE(access_token.has_value());
                               EXPECT_EQ(std::string::npos, access_token->find(credential));
                               last_call_complete = true;
                             });

  RunLoopUntil([&] { return last_call_complete; });
}

}  // namespace
}  // namespace e2e_dev
