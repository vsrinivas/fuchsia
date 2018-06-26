// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/service_account_token_provider.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/network_wrapper/fake_network_wrapper.h"

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

namespace {

constexpr fxl::StringView kTestConfig =
    "{"
    "\"private_key\": \""
    "-----BEGIN RSA PRIVATE KEY-----\\n"
    "MIIBOQIBAAJBALTlyNACX5j/oFWdgy/KvAZL9qj+eNuhXGBSvQM9noaPKqVhOXFH\\n"
    "hycW+TBzlHzj4Ga5uGtVJNzZaxdpfbqxV1cCAwEAAQJAZDJShESMRuZwHHveSf51\\n"
    "Hte8i+ZHcv9xdzjc0Iq037pGGmHh/TiNNZPtqgVbxQuGGdGQqJ54DMpz3Ja2ck1V\\n"
    "wQIhAOMyXwq0Se8+hCXFFFIo6QSVpDn5ZnXTyz+GBdiwkVXZAiEAy9TIRCCUd9j+\\n"
    "cy77lTCx6k6Pw5lY1LM5jTUR7dAD6K8CIBie1snUK8bvYWauartUj5vdk4Rs0Huo\\n"
    "Tfg+T9fhmn5RAiB5nfEL7SCIzbksgqjroE1Xjx5qR5Hf/zvki/ixmz7p0wIgdxLS\\n"
    "T/hN67jcu9a+/2geGTnk1ku2nhVlxS7UPCTq0os=\\n"
    "-----END RSA PRIVATE KEY-----"
    "\","
    "\"client_email\": \"fake_email@example.com\","
    "\"client_id\": \"fake_id\""
    "}";

constexpr fxl::StringView kWrongKeyTestConfig =
    "{"
    "\"private_key\": \""
    "-----BEGIN DSA PRIVATE KEY-----\\n"
    "MIH4AgEAAkEAteW2IBzioOu0aNGrQFv5RZ6VxS8NAyuNwvOrmjq8pxJSzTyrwD52\\n"
    "9XJmNVVXv/UWKvyPtr0rzrsJVpSzCEwaewIVAJT9/8i3lQrQEeACuO9bwzaG28Lh\\n"
    "AkAnvmU9Ogz6eTof5V58Lv1f8uKF6ZujgVb+Wc1gudx8wKIexKUBhE7rsnJUfLYw\\n"
    "HMXC8xZ5XJTEYog2U0vLKke7AkEApEq8XBO8qwEzP3VicpC/Huxa/zNZ2lveNgWm\\n"
    "tr089fvp3PSf4DwKTOKGZyg9NYsOSCfaCSvkWMeFCW4Y7XTpTAIUV9YTY3SlInIv\\n"
    "Ho2twE3HuzNZpLQ=\\n"
    "-----END DSA PRIVATE KEY-----\\n"
    "\","
    "\"client_email\": \"fake_email@example.com\","
    "\"client_id\": \"fake_id\""
    "}";

class ServiceAccountTokenProviderTest : public gtest::TestLoopFixture {
 public:
  ServiceAccountTokenProviderTest()
      : network_wrapper_(dispatcher()),
        token_provider_(&network_wrapper_, "user_id") {}

 protected:
  std::string GetConfigFile(fxl::StringView config) {
    std::string path;
    if (!dir_.NewTempFile(&path)) {
      ADD_FAILURE() << "Unable to create file.";
      return "";
    }
    if (!files::WriteFile(path, config.data(), config.size())) {
      ADD_FAILURE() << "Unable to write file.";
      return "";
    }

    return path;
  }

  std::string GetSuccessResponseBody(std::string token, size_t expiration) {
    rapidjson::StringBuffer string_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

    writer.StartObject();

    writer.Key("idToken");
    writer.String(token);

    writer.Key("expiresIn");
    writer.String(fxl::NumberToString(expiration));

    writer.EndObject();

    return std::string(string_buffer.GetString(), string_buffer.GetSize());
  }

  http::URLResponse GetResponse(http::HttpErrorPtr error, uint32_t status,
                                std::string body) {
    http::URLResponse response;
    response.error = std::move(error);
    response.status_code = status;
    fsl::SizedVmo buffer;
    if (!fsl::VmoFromString(body, &buffer)) {
      ADD_FAILURE() << "Unable to convert string to Vmo.";
    }
    response.body = http::URLBody::New();
    response.body->set_sized_buffer(std::move(buffer).ToTransport());
    return response;
  }

  bool GetToken(std::string api_key,
                fuchsia::modular::auth::FirebaseTokenPtr* token,
                fuchsia::modular::auth::AuthErr* error) {
    bool called;
    token_provider_.GetFirebaseAuthToken(
        api_key,
        callback::Capture(callback::SetWhenCalled(&called), token, error));
    RunLoopUntilIdle();
    return called;
  }

  files::ScopedTempDir dir_;
  network_wrapper::FakeNetworkWrapper network_wrapper_;
  ServiceAccountTokenProvider token_provider_;
};

TEST_F(ServiceAccountTokenProviderTest, GetToken) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  network_wrapper_.SetResponse(
      GetResponse(nullptr, 200, GetSuccessResponseBody("token", 3600)));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;
  ASSERT_TRUE(GetToken("api_key", &token, &error));
  ASSERT_EQ(fuchsia::modular::auth::Status::OK, error.status) << error.message;
  ASSERT_EQ("token", token->id_token);
}

TEST_F(ServiceAccountTokenProviderTest, GetTokenFromCache) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  network_wrapper_.SetResponse(
      GetResponse(nullptr, 200, GetSuccessResponseBody("token", 3600)));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::OK, error.status) << error.message;
  EXPECT_EQ("token", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.ResetRequest();
  network_wrapper_.SetResponse(
      GetResponse(nullptr, 200, GetSuccessResponseBody("token2", 3600)));

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::OK, error.status) << error.message;
  EXPECT_EQ("token", token->id_token);
  EXPECT_FALSE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenProviderTest, GetTokenNoCacheCache) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  network_wrapper_.SetResponse(
      GetResponse(nullptr, 200, GetSuccessResponseBody("token", 0)));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::OK, error.status) << error.message;
  EXPECT_EQ("token", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());

  network_wrapper_.SetResponse(
      GetResponse(nullptr, 200, GetSuccessResponseBody("token2", 0)));

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::OK, error.status) << error.message;
  EXPECT_EQ("token2", token->id_token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenProviderTest, IncorrectCredentials) {
  EXPECT_FALSE(token_provider_.LoadCredentials(GetConfigFile("")));
  EXPECT_FALSE(token_provider_.LoadCredentials(GetConfigFile("{}")));
  EXPECT_FALSE(
      token_provider_.LoadCredentials(GetConfigFile(kWrongKeyTestConfig)));
}

TEST_F(ServiceAccountTokenProviderTest, NetworkError) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  auto network_error = http::HttpError::New();
  network_error->description = "Error";

  network_wrapper_.SetResponse(GetResponse(std::move(network_error), 0, ""));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::NETWORK_ERROR, error.status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenProviderTest, AuthenticationError) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  network_wrapper_.SetResponse(GetResponse(nullptr, 401, "Unauthorized"));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::OAUTH_SERVER_ERROR, error.status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

TEST_F(ServiceAccountTokenProviderTest, ResponseFormatError) {
  ASSERT_TRUE(token_provider_.LoadCredentials(GetConfigFile(kTestConfig)));

  network_wrapper_.SetResponse(GetResponse(nullptr, 200, ""));

  fuchsia::modular::auth::FirebaseTokenPtr token;
  fuchsia::modular::auth::AuthErr error;

  ASSERT_TRUE(GetToken("api_key", &token, &error));
  EXPECT_EQ(fuchsia::modular::auth::Status::BAD_RESPONSE, error.status);
  EXPECT_FALSE(token);
  EXPECT_TRUE(network_wrapper_.GetRequest());
}

}  // namespace
}  // namespace service_account
