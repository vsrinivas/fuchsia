// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/fuchsia_http_client.h"

#include <lib/async/cpp/task.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/network_wrapper/fake_network_wrapper.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {
namespace utils {

namespace http = ::fuchsia::net::oldhttp;

using http::HttpHeader;
using lib::clearcut::HTTPRequest;
using lib::clearcut::HTTPResponse;
using lib::statusor::StatusOr;
using network_wrapper::FakeNetworkWrapper;
using network_wrapper::NetworkWrapper;

namespace {

// Makes a string that is three times longer than the size of a SocketDrainer
// buffer so that we exercise our implementation of
// SocketDrainer::Client.
std::string MakeLongString() {
  return std::string(64 * 1024, 'a') + std::string(64 * 1024, 'b') + std::string(64 * 1024, 'c');
}
}  // namespace

class FuchsiaHTTPClientTest : public ::gtest::TestLoopFixture {
 public:
  FuchsiaHTTPClientTest()
      : ::gtest::TestLoopFixture(),
        network_wrapper_(dispatcher()),
        http_client(new FuchsiaHTTPClient(&network_wrapper_, dispatcher())) {}

  void SetHttpResponse(const std::string& body, uint32_t status_code,
                       std::vector<HttpHeader> headers) {
    http::URLResponse url_response;
    if (!body.empty()) {
      url_response.body = http::URLBody::New();
      url_response.body->set_stream(fsl::WriteStringToSocket(body));
    }
    url_response.status_code = status_code;
    url_response.headers = std::move(headers);
    network_wrapper_.SetResponse(std::move(url_response));
  }

  void SetHttpResponse(zx::socket body, uint32_t status_code, std::vector<HttpHeader> headers) {
    http::URLResponse url_response;
    url_response.body = http::URLBody::New();
    url_response.body->set_stream(std::move(body));
    url_response.status_code = status_code;
    url_response.headers = std::move(headers);
    network_wrapper_.SetResponse(std::move(url_response));
  }

  void SetNetworkErrorResponse(std::string error_message) {
    http::URLResponse url_response;
    url_response.url = "https://www.example.com";
    url_response.error = http::HttpError::New();
    url_response.error->description = std::move(error_message);
    network_wrapper_.SetResponse(std::move(url_response));
  }

  // Invokes Post() in another thread (because Post() is not allowed to be
  // invoked in the dispather's thread) and waits for Post() to complete and
  // returns the value of Post().
  std::future<StatusOr<HTTPResponse>> PostString(const std::string& body) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    // Since Post() returns a future and async returns a future here we have
    // a future<future>.
    auto future_future = std::async(std::launch::async, [this, body, deadline] {
      auto future = http_client->Post(HTTPRequest("http://www.test.com", body), deadline);
      return future;
    });

    // Wait for future<future> returned by async. Its value is the future
    // returned by Post().
    return future_future.get();
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
    std::vector<HttpHeader> response_headers_to_use;
    if (include_response_headers) {
      response_headers_to_use = {HttpHeader{.name = "name1", .value = "value1"},
                                 HttpHeader{.name = "name2", .value = "value2"}};
    }
    SetHttpResponse(response_body_to_use, http_response_code, response_headers_to_use);
    auto response_future = PostString("Request");
    RunLoopUntilIdle();
    // Note that here we are violating our own mandate to not wait on the
    // future returned from Post() in the dispather's thread. The reason this
    // is OK is that we inovked RunLoopUntilIdle() so we know that the future
    // returned form Post() has already been prepared so that get() will not
    // block.
    auto response_or = response_future.get();
    EXPECT_TRUE(response_or.ok());
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

 private:
  FakeNetworkWrapper network_wrapper_;

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
  SetNetworkErrorResponse("Something bad happened.");
  auto response_future = PostString("Request");
  RunLoopUntilIdle();
  // Note that here we are violating our own mandate to not wait on the
  // future returned from Post() in the dispather's thread. The reason this
  // is OK is that we inovked RunLoopUntilIdle() so we know that the future
  // returned form Post() has already been prepared so that get() will not
  // block.
  auto response_or = response_future.get();
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::INTERNAL);
  EXPECT_EQ(response_or.status().error_message(),
            "https://www.example.com error Something bad happened.");
}

TEST_F(FuchsiaHTTPClientTest, TimeoutWithNoResponse) {
  // Do not prepare any response. This causes the FakeNetworkWrapper to
  // never return one so that we will get a timeout after 1 second.

  auto response_future = PostString("Request");

  RunLoopFor(zx::msec(100));
  ASSERT_TRUE(response_future.valid());
  EXPECT_EQ(std::future_status::timeout, response_future.wait_for(std::chrono::microseconds(1)));

  RunLoopFor(zx::sec(1));

  ASSERT_EQ(std::future_status::ready, response_future.wait_for(std::chrono::microseconds(1)));
  auto response_or = response_future.get();
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::DEADLINE_EXCEEDED);
}

}  // namespace utils
}  // namespace cobalt
