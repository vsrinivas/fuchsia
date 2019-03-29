// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/service_account_token_minter.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/network_wrapper/fake_network_wrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/ledger/lib/firebase_auth/testing/service_account_test_constants.h"
#include "src/ledger/lib/firebase_auth/testing/service_account_test_util.h"
#include "src/lib/files/file.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

namespace {

class ServiceAccountTokenMinterTest : public gtest::TestLoopFixture {
 public:
  ServiceAccountTokenMinterTest()
      : network_wrapper_(dispatcher()),
        token_minter_(&network_wrapper_,
                      Credentials::Parse(kTestServiceAccountConfig),
                      "user_id") {}

 protected:
  bool GetFirebaseToken(std::string api_key,
                        ServiceAccountTokenMinter::GetTokenResponse* response) {
    bool called;
    token_minter_.GetFirebaseToken(
        "api_key",
        callback::Capture(callback::SetWhenCalled(&called), response));
    RunLoopUntilIdle();
    return called;
  }

  std::string GetClientId() { return token_minter_.GetClientId(); }

  network_wrapper::FakeNetworkWrapper network_wrapper_;
  ServiceAccountTokenMinter token_minter_;
};

TEST_F(ServiceAccountTokenMinterTest, GetClientId) {
  EXPECT_FALSE(GetClientId().empty());
}

TEST_F(ServiceAccountTokenMinterTest, GetFirebaseToken) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 3600)));

  ServiceAccountTokenMinter::GetTokenResponse resp;
  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::OK, resp.status);
  ASSERT_EQ("token", resp.id_token);
}

TEST_F(ServiceAccountTokenMinterTest, GetFirebaseTokenFromCache) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 3600)));

  ServiceAccountTokenMinter::GetTokenResponse resp;

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::OK, resp.status);
  ASSERT_EQ("token", resp.id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.ResetRequest();
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token2", 3600)));

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::OK, resp.status);
  ASSERT_EQ("token", resp.id_token);
  EXPECT_FALSE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenMinterTest, GetFirebaseTokenNoCacheCache) {
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token", 0)));

  ServiceAccountTokenMinter::GetTokenResponse resp;

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::OK, resp.status);
  ASSERT_EQ("token", resp.id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.ResetRequest();
  network_wrapper_.SetResponse(GetResponseForTest(
      nullptr, 200, GetSuccessResponseBodyForTest("token2", 3600)));

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::OK, resp.status);
  ASSERT_EQ("token2", resp.id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenMinterTest, NetworkError) {
  auto network_status = http::HttpError::New();
  network_status->description = "Error";

  network_wrapper_.SetResponse(
      GetResponseForTest(std::move(network_status), 0, ""));

  ServiceAccountTokenMinter::GetTokenResponse resp;

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::NETWORK_ERROR, resp.status);
  EXPECT_TRUE(resp.id_token.empty());
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenMinterTest, AuthenticationError) {
  network_wrapper_.SetResponse(
      GetResponseForTest(nullptr, 401, "Unauthorized"));

  ServiceAccountTokenMinter::GetTokenResponse resp;

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::AUTH_SERVER_ERROR, resp.status);
  EXPECT_TRUE(resp.id_token.empty());
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenMinterTest, ResponseFormatError) {
  network_wrapper_.SetResponse(GetResponseForTest(nullptr, 200, ""));

  ServiceAccountTokenMinter::GetTokenResponse resp;

  ASSERT_TRUE(GetFirebaseToken("api_key", &resp));
  ASSERT_EQ(ServiceAccountTokenMinter::Status::BAD_RESPONSE, resp.status);
  EXPECT_TRUE(resp.id_token.empty());
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

}  // namespace
}  // namespace service_account
