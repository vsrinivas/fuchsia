// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_with_loop.h"
#include "lib/network_wrapper/fake_network_wrapper.h"

namespace cobalt {
namespace utils {

using clearcut::HTTPRequest;
using clearcut::HTTPResponse;
using network_wrapper::FakeNetworkWrapper;
using tensorflow_statusor::StatusOr;

class FuchsiaHTTPClientTest : public ::gtest::TestWithLoop {
 public:
  FuchsiaHTTPClientTest()
      : ::gtest::TestWithLoop(),
        network_wrapper_(dispatcher()),
        delete_after_post_(false),
        http_client_(new FuchsiaHTTPClient(&network_wrapper_, dispatcher())) {}

  void PrepareResponse(const std::string& body, uint32_t status_code = 200) {
    network_wrapper_.SetStringResponse(body, status_code);
  }

  void PrepareResponse(zx::socket body, uint32_t status_code = 200) {
    network_wrapper_.SetSocketResponse(std::move(body), status_code);
  }

  template <class Rep, class Period>
  bool WaitForPostFor(const std::chrono::duration<Rep, Period>& duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (post_sent_) {
      return true;
    }
    return post_sent_cv_.wait_for(lock, duration,
                                  [this] { return post_sent_; });
  }

  std::future<StatusOr<HTTPResponse>> PostString(const std::string& body) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    auto future = std::async(std::launch::async, [this, body, deadline] {
      HTTPRequest request;
      request.url = "http://www.test.com";
      request.body = body;
      auto post_future = http_client_->Post(request, deadline);
      if (delete_after_post_) {
        delete_after_post_ = false;
        http_client_.reset(nullptr);
      }
      {
        std::unique_lock<std::mutex> lock(mutex_);
        post_sent_ = true;
      }
      post_sent_cv_.notify_one();
      return post_future.get();
    });
    // Wait up to 1 second for the post to be sent.
    EXPECT_TRUE(WaitForPostFor(std::chrono::seconds(1)));
    return future;
  }

  void DeleteHttpClientAfterPost() { delete_after_post_ = true; }

 private:
  FakeNetworkWrapper network_wrapper_;
  bool delete_after_post_;
  std::unique_ptr<FuchsiaHTTPClient> http_client_;
  std::mutex mutex_;
  bool post_sent_;
  std::condition_variable post_sent_cv_;
};

TEST_F(FuchsiaHTTPClientTest, MakePostAndGet) {
  PrepareResponse("Response");
  auto response_future = PostString("Request");
  RunLoopFor(zx::sec(5));
  EXPECT_EQ(response_future.wait_for(std::chrono::milliseconds(1)),
            std::future_status::ready);
  auto response_or = response_future.get();
  EXPECT_TRUE(response_or.ok());
  auto response = response_or.ConsumeValueOrDie();
  EXPECT_EQ(response.response, "Response");
}

TEST_F(FuchsiaHTTPClientTest, TestTimeout) {
  auto response_future = PostString("Request");

  RunLoopFor(zx::msec(100));
  EXPECT_NE(std::future_status::ready,
            response_future.wait_for(std::chrono::milliseconds(1)));

  RunLoopFor(zx::sec(1));
  EXPECT_EQ(std::future_status::ready,
            response_future.wait_for(std::chrono::milliseconds(1)));

  auto response_or = response_future.get();
  ASSERT_FALSE(response_or.ok());
  EXPECT_EQ(response_or.status().error_code(),
            util::StatusCode::DEADLINE_EXCEEDED);
}

TEST_F(FuchsiaHTTPClientTest, WaitAfterRelease) {
  zx::socket socket_in, socket_out;
  ASSERT_EQ(zx::socket::create(0u, &socket_in, &socket_out), ZX_OK);
  PrepareResponse(std::move(socket_in));

  DeleteHttpClientAfterPost();
  auto response_future = PostString("Request");

  const char message[] = "Response";
  for (const char* it = message; *it; ++it) {
    RunLoopUntilIdle();
    size_t bytes_written;
    socket_out.write(0u, it, 1, &bytes_written);
    EXPECT_EQ(1u, bytes_written);
    EXPECT_NE(std::future_status::ready,
              response_future.wait_for(std::chrono::milliseconds(1)));
  }
  socket_out.reset();
  RunLoopUntilIdle();
  ASSERT_EQ(std::future_status::ready,
            response_future.wait_for(std::chrono::milliseconds(1)));

  auto response_or = response_future.get();
  EXPECT_TRUE(response_or.ok());
  auto response = response_or.ConsumeValueOrDie();
  EXPECT_EQ(response.response, "Response");
}

}  // namespace utils
}  // namespace cobalt
