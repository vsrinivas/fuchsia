// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/service_account_token_manager.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/network_wrapper/fake_network_wrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/ledger/lib/firebase_auth/testing/service_account_test_constants.h"
#include "src/ledger/lib/firebase_auth/testing/service_account_test_util.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

namespace {

class ServiceAccountTokenManagerTest : public gtest::TestLoopFixture {
 public:
  ServiceAccountTokenManagerTest()
      : network_wrapper_(dispatcher()),
        token_manager_(&network_wrapper_,
                       Credentials::Parse(kTestServiceAccountConfig),
                       "user_id") {}

 protected:
  bool GetToken(std::string api_key, fuchsia::auth::FirebaseTokenPtr* token,
                fuchsia::auth::Status* status) {
    bool called;
    token_manager_.GetFirebaseToken(
        {"google", "", ""}, /*app_config*/
        "",                 /*user_profile_id*/
        "",                 /*audience*/
        api_key,
        callback::Capture(callback::SetWhenCalled(&called), status, token));
    RunLoopUntilIdle();
    return called;
  }

  network_wrapper::FakeNetworkWrapper network_wrapper_;
  ServiceAccountTokenManager token_manager_;
};

TEST_F(ServiceAccountTokenManagerTest, GetToken) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 3600)));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;
  ASSERT_TRUE(GetToken("api_key", &token, &status));
  ASSERT_EQ(fuchsia::auth::Status::OK, status);
  ASSERT_EQ("token", token->id_token);
}

TEST_F(ServiceAccountTokenManagerTest, GetTokenFromCache) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 3600)));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::OK, status);
  EXPECT_EQ("token", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.ResetRequest();
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token2", 3600)));

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::OK, status);
  EXPECT_EQ("token", token->id_token);
  EXPECT_FALSE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenManagerTest, GetTokenNoCacheCache) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 0)));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::OK, status);
  EXPECT_EQ("token", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token2", 0)));

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::OK, status);
  EXPECT_EQ("token2", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenManagerTest, NetworkError) {
  auto network_status = http::HttpError::New();
  network_status->description = "Error";

  network_wrapper_.SetResponse(
      GetResponseForTest(std::move(network_status), 0, ""));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::NETWORK_ERROR, status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenManagerTest, AuthenticationError) {
  network_wrapper_.SetResponse(
      GetResponseForTest(nullptr, 401, "Unauthorized"));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::AUTH_PROVIDER_SERVER_ERROR, status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenManagerTest, ResponseFormatError) {
  network_wrapper_.SetResponse(GetResponseForTest(nullptr, 200, ""));

  fuchsia::auth::FirebaseTokenPtr token;
  fuchsia::auth::Status status;

  ASSERT_TRUE(GetToken("api_key", &token, &status));
  EXPECT_EQ(fuchsia::auth::Status::AUTH_PROVIDER_SERVER_ERROR, status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

}  // namespace
}  // namespace service_account
