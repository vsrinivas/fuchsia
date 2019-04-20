// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/lib/oauth/oauth_response.h"

#include <stdlib.h>

#include <string>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace auth_providers {
namespace oauth {

using fuchsia::auth::AuthProviderStatus;

namespace {

namespace http = ::fuchsia::net::oldhttp;

http::URLResponse FakeError(int32_t code, std::string reason) {
  http::URLResponse response;
  response.error = http::HttpError::New();
  response.error->code = code;
  response.error->description = reason;
  return response;
}

http::URLResponse FakeSuccess(int32_t code, const std::string& body) {
  http::URLResponse response;
  response.body = http::URLBody::New();
  response.body->set_stream(fsl::WriteStringToSocket(body));
  response.status_code = code;

  http::HttpHeader content_length_header;
  content_length_header.name = "content-length";
  content_length_header.value = fxl::NumberToString(body.size());
  response.headers.push_back(std::move(content_length_header));

  return response;
}

void VerifyOAuthResponse(OAuthResponse got, OAuthResponse want) {
  EXPECT_EQ(want.status, got.status);
  EXPECT_TRUE(got.error_description.find(want.error_description) !=
              std::string::npos);

  if (want.json_response.IsObject() && got.json_response.IsObject()) {
    for (auto& m : want.json_response.GetObject()) {
      auto key = m.name.GetString();
      rapidjson::Value::ConstMemberIterator itr =
          got.json_response.FindMember(key);
      EXPECT_TRUE(itr != got.json_response.MemberEnd());

      if (m.value.IsString())
        EXPECT_EQ(got.json_response[key].GetString(), itr->value.GetString());
      if (m.value.IsUint64())
        EXPECT_EQ(got.json_response[key].GetUint64(), itr->value.GetUint64());
      if (m.value.IsInt())
        EXPECT_EQ(got.json_response[key].GetInt(), itr->value.GetInt());
      if (m.value.IsInt64())
        EXPECT_EQ(got.json_response[key].GetInt64(), itr->value.GetInt64());
      if (m.value.IsBool())
        EXPECT_EQ(got.json_response[key].GetBool(), itr->value.GetBool());
      if (m.value.IsArray()) {
        for (uint i = 0; i < want.json_response[key].GetArray().Size(); i++) {
          EXPECT_EQ(want.json_response[key].GetArray()[i],
                    got.json_response[key].GetArray()[i]);
        }
      }
    }
  }
}

}  // namespace

class OAuthResponseTest : public ::testing::Test {
 protected:
  OAuthResponseTest() {}

  ~OAuthResponseTest() override {}
};

TEST_F(OAuthResponseTest, CheckParseOAuthResponse) {
  rapidjson::Document no_json_body;
  VerifyOAuthResponse(ParseOAuthResponse(FakeError(-2, "Bad request")),
                      OAuthResponse(AuthProviderStatus::NETWORK_ERROR,
                                    "Bad request", std::move(no_json_body)));

  rapidjson::Document json_200;
  json_200.Parse("{\"token\":\"xyz\"}");
  VerifyOAuthResponse(
      ParseOAuthResponse(
          FakeSuccess(200, modular::JsonValueToPrettyString(json_200))),
      OAuthResponse(AuthProviderStatus::OK, "", std::move(json_200)));

  rapidjson::Document json_400;
  json_400.Parse("{\"error\":\"invalid_grant\"}");
  VerifyOAuthResponse(ParseOAuthResponse(FakeSuccess(
                          400, modular::JsonValueToPrettyString(json_400))),
                      OAuthResponse(AuthProviderStatus::REAUTH_REQUIRED, "400",
                                    std::move(json_400)));

  rapidjson::Document json_400_br;
  json_400_br.Parse("{\"error\":\"invalid_argument\"}");
  VerifyOAuthResponse(ParseOAuthResponse(FakeSuccess(
                          400, modular::JsonValueToPrettyString(json_400_br))),
                      OAuthResponse(AuthProviderStatus::OAUTH_SERVER_ERROR,
                                    "400", std::move(json_400_br)));

  rapidjson::Document json_401;
  json_401.Parse("{\"error\":\"invalid_client\"}");
  VerifyOAuthResponse(ParseOAuthResponse(FakeSuccess(
                          401, modular::JsonValueToPrettyString(json_401))),
                      OAuthResponse(AuthProviderStatus::OAUTH_SERVER_ERROR,
                                    "401", std::move(json_401)));

  rapidjson::Document json_403;
  json_403.Parse("{\"error\":\"access_denied\"}");
  VerifyOAuthResponse(ParseOAuthResponse(FakeSuccess(
                          403, modular::JsonValueToPrettyString(json_403))),
                      OAuthResponse(AuthProviderStatus::OAUTH_SERVER_ERROR,
                                    "403", std::move(json_403)));
}

}  // namespace oauth
}  // namespace auth_providers
