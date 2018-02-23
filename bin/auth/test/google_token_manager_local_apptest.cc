// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager unit tests using DEV auth provider.

#include <memory>
#include <string>

#include "garnet/lib/callback/capture.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/auth/fidl/auth_provider.fidl.h"
#include "lib/auth/fidl/token_manager.fidl-sync.h"
#include "lib/auth/fidl/token_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

namespace e2e_google_local {
namespace {

const std::string kTestUserId = "tq_auth_user_1";
const auth::AuthProviderType kGoogleAuthProvider =
    auth::AuthProviderType::GOOGLE;

class FakeAuthenticationUIContextImpl : public ::auth::AuthenticationUIContext {
 public:
  void StartOverlay(
      ::f1dl::InterfaceHandle<mozart::ViewOwner> view_owner) override {
    FXL_LOG(INFO)
        << "FakeAuthenticationUIContextImpl::StartOverlay() is unimplemented.";
  }

  void StopOverlay() override {
    FXL_LOG(INFO)
        << "FakeAuthenticationUIContextImpl::StopOverlay() is unimplemented.";
  }
};

class GoogleTokenManagerAppTest : public gtest::TestWithMessageLoop {
 public:
  GoogleTokenManagerAppTest()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  }

  ~GoogleTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    app::Services services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "token_manager";
    launch_info->service_request = services.NewRequest();
    {
      std::ostringstream stream;
      stream << "--verbose=" << fxl::GetVlogVerbosity();
      launch_info->arguments.push_back(stream.str());
    }
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), app_controller_.NewRequest());
    app_controller_.set_error_handler([] {
      FXL_LOG(ERROR) << "Error in connecting to TokenManagerFactory service.";
    });

    services.ConnectToService(token_mgr_factory_.NewRequest());

    auto google_config_ptr = auth::AuthProviderConfig::New();
    google_config_ptr->auth_provider_type = kGoogleAuthProvider;
    google_config_ptr->url = "google_auth_provider";
    auth_provider_configs_.push_back(std::move(google_config_ptr));

    token_mgr_factory_->GetTokenManager(kTestUserId,
                                        std::move(auth_provider_configs_),
                                        token_mgr_.NewRequest());
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr app_controller_;
  f1dl::Array<auth::AuthProviderConfigPtr> auth_provider_configs_;

 protected:
  auth::TokenManagerPtr token_mgr_;
  auth::TokenManagerFactoryPtr token_mgr_factory_;
  f1dl::BindingSet<auth::AuthenticationUIContext> ui_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GoogleTokenManagerAppTest);
};

TEST_F(GoogleTokenManagerAppTest, Authorize) {
  auth::AuthenticationUIContextPtr auth_ui_context_;
  FakeAuthenticationUIContextImpl ui_impl;
  ui_bindings_.AddBinding(&ui_impl, auth_ui_context_.NewRequest());

  auth::Status status;
  auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(kGoogleAuthProvider, std::move(auth_ui_context_),
                        callback::Capture(MakeQuitTask(), &status, &user_info));
  EXPECT_TRUE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::INTERNAL_ERROR, status);
  // TODO(ukode): Validate user_info contents
}

TEST_F(GoogleTokenManagerAppTest, GetAccessToken) {
  auto scopes = f1dl::Array<f1dl::String>::New(0);
  auth::Status status;
  f1dl::String access_token;

  token_mgr_->GetAccessToken(
      kGoogleAuthProvider, "", std::move(scopes),
      callback::Capture(MakeQuitTask(), &status, &access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
}

TEST_F(GoogleTokenManagerAppTest, GetIdToken) {
  auth::Status status;
  f1dl::String id_token;

  token_mgr_->GetIdToken(kGoogleAuthProvider, "",
                         callback::Capture(MakeQuitTask(), &status, &id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
}

TEST_F(GoogleTokenManagerAppTest, GetFirebaseToken) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;

  token_mgr_->GetFirebaseToken(
      kGoogleAuthProvider, "",
      callback::Capture(MakeQuitTask(), &status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_FALSE(firebase_token);
}

TEST_F(GoogleTokenManagerAppTest, EraseAllTokens) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;

  token_mgr_->DeleteAllTokens(kGoogleAuthProvider,
                              callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
}

}  // namespace
}  // namespace e2e_google_local

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  {
    fsl::MessageLoop message_loop;
    auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
