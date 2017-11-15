// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Token manager unit tests using DEV auth provider.

#include <memory>
#include <string>

#include "garnet/bin/auth/test/capture.h"
#include "garnet/bin/auth/test/test_with_message_loop.h"
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
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

namespace e2e_dev {
namespace {

const std::string kTestUserId = "tq_auth_user_1";
const auth::AuthProviderType kDevAuthProvider = auth::AuthProviderType::DEV;

class DevTokenManagerAppTest : public ::auth::TestWithMessageLoop {
 public:
  DevTokenManagerAppTest()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  }

  ~DevTokenManagerAppTest() {}

 protected:
  // ::testing::Test:
  void SetUp() override {
    app::ServiceProviderPtr services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "token_manager";
    launch_info->services = services.NewRequest();
    {
      std::ostringstream stream;
      stream << "--verbose=" << fxl::GetVlogVerbosity();
      launch_info->arguments.push_back(stream.str());
  }
  application_context_->launcher()->CreateApplication(
      std::move(launch_info), app_controller_.NewRequest());
  app_controller_.set_connection_error_handler(
      [] { FXL_LOG(ERROR) << "Error in connecting to TokenManagerFactory service."; });

  app::ConnectToService(services.get(), token_mgr_factory_.NewRequest());

  auto dev_config_ptr = auth::AuthProviderConfig::New();
  dev_config_ptr->auth_provider_type = kDevAuthProvider;
  dev_config_ptr->url = "dev_auth_provider";
  auth_provider_configs_.push_back(std::move(dev_config_ptr));

  token_mgr_factory_->GetTokenManager(
      kTestUserId, std::move(auth_provider_configs_), token_mgr_.NewRequest());
  }

  void TearDown() override {}

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr app_controller_;
  fidl::Array<auth::AuthProviderConfigPtr> auth_provider_configs_;

 protected:
  auth::TokenManagerPtr token_mgr_;
  auth::TokenManagerFactoryPtr token_mgr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevTokenManagerAppTest);
};

TEST_F(DevTokenManagerAppTest, Authorize) {
  auth::AuthenticationUIContextPtr auth_ui_context;
  auth::Status status;
  auth::UserProfileInfoPtr user_info;

  token_mgr_->Authorize(kDevAuthProvider, std::move(auth_ui_context),
                        auth::Capture(MakeQuitTask(), &status, &user_info));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  // TODO(ukode): Validate user_info contents
}

TEST_F(DevTokenManagerAppTest, GetAccessToken) {
  auto scopes = fidl::Array<fidl::String>::New(0);
  auth::Status status;
  fidl::String access_token;

  token_mgr_->GetAccessToken(
      kDevAuthProvider, "", std::move(scopes),
      auth::Capture(MakeQuitTask(), &status, &access_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(access_token.get().find(":at_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetIdToken) {
  auth::Status status;
  fidl::String id_token;

  token_mgr_->GetIdToken(kDevAuthProvider, "",
                         auth::Capture(MakeQuitTask(), &status, &id_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_TRUE(id_token.get().find(":idt_") != std::string::npos);
}

TEST_F(DevTokenManagerAppTest, GetFirebaseToken) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;

  token_mgr_->GetFirebaseToken(
      kDevAuthProvider, "",
      auth::Capture(MakeQuitTask(), &status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
  EXPECT_FALSE(firebase_token);
}

TEST_F(DevTokenManagerAppTest, EraseAllTokens) {
  auth::Status status;
  auth::FirebaseTokenPtr firebase_token;

  token_mgr_->DeleteAllTokens(kDevAuthProvider,
                              auth::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(auth::Status::OK, status);
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
    fsl::MessageLoop message_loop;
    auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
