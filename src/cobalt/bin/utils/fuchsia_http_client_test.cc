// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/fuchsia_http_client.h"

#include <lib/async/cpp/task.h>

#include <memory>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/sys/cpp/testing/service_directory_provider.h"
#include "src/cobalt/bin/testing/fake_http_loader.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace utils {

using lib::clearcut::HTTPRequest;
using lib::clearcut::HTTPResponse;
using lib::statusor::StatusOr;
using ::testing::HasSubstr;

namespace {

// Makes a string that is three times longer than the size of a SocketDrainer
// buffer so that we exercise our implementation of
// SocketDrainer::Client.
std::string MakeLongString() {
  return std::string(64 * 1024, 'a') + std::string(64 * 1024, 'b') + std::string(64 * 1024, 'c');
}

std::vector<uint8_t> ToBytes(std::string str) {
  return std::vector<uint8_t>(str.begin(), str.end());
}

}  // namespace

class FuchsiaHTTPClientTest : public ::gtest::TestLoopFixture {
 public:
  FuchsiaHTTPClientTest()
      : ::gtest::TestLoopFixture(),
        service_directory_provider_(new sys::testing::ServiceDirectoryProvider(dispatcher())),
        http_client(new FuchsiaHTTPClient(dispatcher(), [this] {
          return service_directory_provider_->service_directory()
              ->Connect<fuchsia::net::http::Loader>();
        })) {}

  void SetUp() override {
    loader_ = std::make_unique<FakeHTTPLoader>(dispatcher());
    service_directory_provider_->AddService(loader_->GetHandler());
  }

  void SetHttpResponse(const std::string& body, uint32_t status_code,
                       std::vector<fuchsia::net::http::Header> headers = {}) {
    fuchsia::net::http::Response response;
    if (!body.empty()) {
      response.set_body(fsl::WriteStringToSocket(body));
    }
    response.set_status_code(status_code);
    response.set_headers(std::move(headers));
    loader_->SetResponse(std::move(response));
  }

  void SetHttpResponse(zx::socket body, uint32_t status_code,
                       std::vector<fuchsia::net::http::Header> headers) {
    fuchsia::net::http::Response response;
    response.set_body(std::move(body));
    response.set_status_code(status_code);
    response.set_headers(std::move(headers));
    loader_->SetResponse(std::move(response));
  }

  void SetNetworkErrorResponse(fuchsia::net::http::Error error) {
    fuchsia::net::http::Response response;
    response.set_final_url("https://www.example.com");
    response.set_error(error);
    loader_->SetResponse(std::move(response));
  }

  void SetResponseDelay(zx::duration response_delay) { loader_->SetResponseDelay(response_delay); }

  // Invokes Post() in another thread (because Post() is not allowed to be
  // invoked in the dispather's thread) and waits for Post() to complete and
  // returns the value of Post().
  std::future<StatusOr<HTTPResponse>> PostString(const std::string& body) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    return std::async(std::launch::async, [this, body, deadline]() mutable {
      return http_client->PostSync(HTTPRequest("http://www.test.com", body), deadline);
    });
  }

  StatusOr<HTTPResponse> PostStringAndWait(std::string request, bool wait_in_seconds = false) {
    auto response_future = PostString("Request");
    auto start_time = std::chrono::steady_clock::now();
    while (response_future.wait_for(std::chrono::microseconds(1)) != std::future_status::ready) {
      if (wait_in_seconds) {
        RunLoopFor(zx::msec(1));
      } else {
        RunLoopUntilIdle();
      }
      if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(1)) {
        return util::Status(util::StatusCode::DEADLINE_EXCEEDED, "Timed out while waiting!");
      }
    }
    // Note that here we are violating our own mandate to not wait on the
    // future returned from Post() in the dispather's thread. The reason this
    // is OK is that we inovked RunLoopUntilIdle() so we know that the future
    // returned form Post() has already been prepared so that get() will not
    // block.
    return response_future.get();
  }

  // Prepares the FakeNetworkWrapper to return a response without a network
  // error and then invokes PostString() and checks the returned HTTPResponse.
  //
  // |response_body_to_use| is the response body that the FakeNetworkWrapper will
  // be asked to return. If this is empty then the |body| field will be empty.
  //
  // |http_response_code| is the code that the FakeNetworkWrapper will be asked
  // to return.
  //
  // |include_response_headers| Should the FakeNetworkWrapper include any
  // response headers.
  void DoPostTest(std::string response_body_to_use, uint32_t http_response_code,
                  bool include_response_headers) {
    std::vector<fuchsia::net::http::Header> response_headers_to_use;
    if (include_response_headers) {
      response_headers_to_use = {
          fuchsia::net::http::Header{.name = ToBytes("name1"), .value = ToBytes("value1")},
          fuchsia::net::http::Header{.name = ToBytes("name2"), .value = ToBytes("value2")}};
    }
    SetHttpResponse(response_body_to_use, http_response_code, response_headers_to_use);
    auto response_or = PostStringAndWait("Request");
    ASSERT_TRUE(response_or.ok());
    auto response = response_or.ConsumeValueOrDie();
    EXPECT_EQ(response.response, response_body_to_use);
    EXPECT_EQ(response.http_code, http_response_code);
    if (include_response_headers) {
      EXPECT_EQ(response.headers.size(), 2);
      EXPECT_EQ(response.headers["name1"], "value1");
      EXPECT_EQ(response.headers["name2"], "value2");
    } else {
      EXPECT_TRUE(response.headers.empty());
    }
  }

  void CrashLoaderService() {
    service_directory_provider_ =
        std::make_unique<sys::testing::ServiceDirectoryProvider>(dispatcher());
    loader_->Unbind();
  }

 private:
  std::unique_ptr<sys::testing::ServiceDirectoryProvider> service_directory_provider_;
  std::unique_ptr<FakeHTTPLoader> loader_;

 public:
  std::unique_ptr<FuchsiaHTTPClient> http_client;
};

TEST_F(FuchsiaHTTPClientTest, EmptyBodyNoHeaders) { DoPostTest("", 100, false); }

TEST_F(FuchsiaHTTPClientTest, EmptyBodyWithHeaders) { DoPostTest("", 101, true); }

TEST_F(FuchsiaHTTPClientTest, ShortBodyNoHeaders) { DoPostTest("Short response", 200, false); }

TEST_F(FuchsiaHTTPClientTest, ShortBodyWithHeaders) { DoPostTest("Short response", 201, true); }

TEST_F(FuchsiaHTTPClientTest, LongBodyNoHeaders) { DoPostTest(MakeLongString(), 202, false); }

TEST_F(FuchsiaHTTPClientTest, LongBodyWithHeaders) { DoPostTest(MakeLongString(), 203, true); }

TEST_F(FuchsiaHTTPClientTest, NetworkError) {
  SetNetworkErrorResponse(fuchsia::net::http::Error::INTERNAL);
  auto response_or = PostStringAndWait("Request");
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::INTERNAL);
  EXPECT_THAT(response_or.status().error_message(), HasSubstr("Internal Error"));
}

TEST_F(FuchsiaHTTPClientTest, TimeoutWithNoResponse) {
  // Do not prepare any response. This causes the FakeNetworkWrapper to
  // never return one so that we will get a timeout after 1 second.

  auto response_future = PostString("Request");

  RunLoopFor(zx::msec(100));
  ASSERT_TRUE(response_future.valid());

  EXPECT_EQ(std::future_status::timeout, response_future.wait_for(std::chrono::microseconds(1)));

  while (response_future.wait_for(std::chrono::microseconds(1)) != std::future_status::ready) {
    RunLoopFor(zx::sec(1));
  }

  ASSERT_EQ(std::future_status::ready, response_future.wait_for(std::chrono::microseconds(1)));
  auto response_or = response_future.get();
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);
}

TEST_F(FuchsiaHTTPClientTest, MultipleSlowResponses) {
  // Set delay for each response
  SetResponseDelay(zx::sec(10));

  // With a 10 second delay, requests should consistently time out.
  SetHttpResponse("Body1", 200);
  auto response_or = PostStringAndWait("Request", true);
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);

  SetHttpResponse("Body2", 200);
  response_or = PostStringAndWait("Request", true);
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);

  SetHttpResponse("Body3", 200);
  response_or = PostStringAndWait("Request", true);
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);

  SetHttpResponse("Body4", 200);
  response_or = PostStringAndWait("Request", true);
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);

  SetHttpResponse("Body5", 200);
  response_or = PostStringAndWait("Request", true);
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);

  // With no delay, all should be fine
  SetResponseDelay(zx::sec(0));
  DoPostTest("Body6", 200, false);
}

TEST_F(FuchsiaHTTPClientTest, HandlesServiceCrash) {
  DoPostTest("", 200, false);

  CrashLoaderService();

  auto response_or = PostStringAndWait("Request");
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::UNAVAILABLE);
  EXPECT_THAT(response_or.status().error_message(), HasSubstr("ZX_ERR_PEER_CLOSED"));
}

}  // namespace utils
}  // namespace cobalt
