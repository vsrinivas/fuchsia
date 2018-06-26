// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/network_wrapper/fake_network_wrapper.h"

namespace cobalt {
namespace utils {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::HTTPRequest;
using clearcut::HTTPResponse;
using network_wrapper::FakeNetworkWrapper;
using network_wrapper::NetworkWrapper;
using tensorflow_statusor::StatusOr;

class CVBool {
 public:
  CVBool() : set_(false) {}

  void Notify() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      set_ = true;
    }
    cv_.notify_all();
  }

  template <class Rep, class Period>
  bool Wait(const std::chrono::duration<Rep, Period>& duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (set_) {
      return true;
    }
    return cv_.wait_for(lock, duration, [this] { return set_; });
  }

  bool Check() {
    std::unique_lock<std::mutex> lock(mutex_);
    return set_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool set_;
};

class TestFuchsiaHTTPClient : public FuchsiaHTTPClient {
 public:
  TestFuchsiaHTTPClient(NetworkWrapper* network_wrapper, async_t* async)
      : FuchsiaHTTPClient(network_wrapper, async) {}

  void HandleResponse(fxl::RefPtr<NetworkRequest> req,
                      http::URLResponse fx_response) override {
    FuchsiaHTTPClient::HandleResponse(req, std::move(fx_response));
    response_handled_.Notify();
  }

  void HandleDeadline(fxl::RefPtr<NetworkRequest> req) override {
    deadline_triggered_.Notify();
    FuchsiaHTTPClient::HandleDeadline(req);
  }

  bool CheckResponseHandled() { return response_handled_.Check(); }
  bool CheckDeadlineTriggered() { return deadline_triggered_.Check(); }

  CVBool response_handled_;
  CVBool deadline_triggered_;
};

class FuchsiaHTTPClientTest : public ::gtest::TestLoopFixture {
 public:
  FuchsiaHTTPClientTest()
      : ::gtest::TestLoopFixture(),
        network_wrapper_(dispatcher()),
        delete_after_post_(false),
        http_client(
            new TestFuchsiaHTTPClient(&network_wrapper_, dispatcher())) {}

  void PrepareResponse(const std::string& body, uint32_t status_code = 200) {
    network_wrapper_.SetStringResponse(body, status_code);
  }

  void PrepareResponse(zx::socket body, uint32_t status_code = 200) {
    network_wrapper_.SetSocketResponse(std::move(body), status_code);
  }

  std::future<StatusOr<HTTPResponse>> PostString(const std::string& body) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    auto future = std::async(std::launch::async, [this, body, deadline] {
      auto post_future =
          http_client->Post(HTTPRequest("http://www.test.com", body), deadline);
      if (delete_after_post_) {
        delete_after_post_ = false;
        http_client.reset(nullptr);
      }
      post_sent_.Notify();
      return post_future.get();
    });
    // Wait up to 10 second for std::async to run. This should happen almost
    // immediately.
    EXPECT_TRUE(post_sent_.Wait(std::chrono::seconds(10)));

    return future;
  }

  template <class F>
  StatusOr<F> RunUntilReady(std::future<StatusOr<F>>* future,
                            zx::duration max_wait,
                            zx::duration increment = zx::msec(100)) {
    zx::duration elapsed;
    while (elapsed < max_wait) {
      elapsed += increment;
      RunLoopFor(increment);
      if (std::future_status::ready ==
          future->wait_for(std::chrono::milliseconds(1))) {
        return future->get();
      }
    }
    return util::Status(util::StatusCode::CANCELLED, "Ran out of time");
  }

  void DeleteHttpClientAfterPost() { delete_after_post_ = true; }

 private:
  FakeNetworkWrapper network_wrapper_;
  bool delete_after_post_;
  CVBool post_sent_;

 public:
  std::unique_ptr<TestFuchsiaHTTPClient> http_client;
};

TEST_F(FuchsiaHTTPClientTest, MakePostAndGet) {
  PrepareResponse("Response");
  auto response_future = PostString("Request");
  RunLoopUntilIdle();
  EXPECT_TRUE(http_client->CheckResponseHandled());
  auto response_or = response_future.get();
  EXPECT_TRUE(response_or.ok());
  auto response = response_or.ConsumeValueOrDie();
  EXPECT_EQ(response.response, "Response");
}

TEST_F(FuchsiaHTTPClientTest, TestTimeout) {
  auto response_future = PostString("Request");

  RunLoopFor(zx::msec(100));
  EXPECT_FALSE(http_client->CheckDeadlineTriggered());

  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(http_client->CheckDeadlineTriggered());

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
    RunLoopFor(zx::sec(1));
    size_t bytes_written;
    socket_out.write(0u, it, 1, &bytes_written);
    EXPECT_EQ(1u, bytes_written);
    EXPECT_NE(std::future_status::ready,
              response_future.wait_for(std::chrono::milliseconds(1)));
  }
  socket_out.reset();

  auto response_or = RunUntilReady(&response_future, zx::sec(1));
  EXPECT_EQ(response_or.status().error_code(), util::StatusCode::OK);
  auto response = response_or.ConsumeValueOrDie();
  EXPECT_EQ(response.response, "Response");
}

}  // namespace utils
}  // namespace cobalt
