// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/network_wrapper/network_wrapper_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/socket.h>

#include "gtest/gtest.h"
#include "lib/backoff/testing/test_backoff.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"

namespace network_wrapper {
namespace {

namespace http = ::fuchsia::net::oldhttp;

const char kRedirectUrl[] = "http://example.com/redirect";

// Url loader that stores the url request for inspection in |request_received|,
// and returns response indicated in |response_to_return|. |response_to_return|
// is moved out in ::Start().
class FakeURLLoader : public http::URLLoader {
 public:
  FakeURLLoader(fidl::InterfaceRequest<http::URLLoader> request,
                http::URLResponse response_to_return,
                http::URLRequest* request_received)
      : binding_(this, std::move(request)),
        response_to_return_(std::move(response_to_return)),
        request_received_(request_received) {}
  ~FakeURLLoader() override {}

  // URLLoader:
  void Start(http::URLRequest request, StartCallback callback) override {
    *request_received_ = std::move(request);
    callback(std::move(response_to_return_));
  }
  void FollowRedirect(FollowRedirectCallback) override {}
  void QueryStatus(QueryStatusCallback) override {}

 private:
  fidl::Binding<http::URLLoader> binding_;
  http::URLResponse response_to_return_;
  http::URLRequest* request_received_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeURLLoader);
};

// Fake implementation of network service, allowing to inspect the last request
// passed to any url loader and set the response that url loaders need to
// return. Response is moved out when url request starts, and needs to be set
// each time.
class FakeNetworkWrapper : public http::HttpService {
 public:
  explicit FakeNetworkWrapper(fidl::InterfaceRequest<http::HttpService> request)
      : binding_(this, std::move(request)) {}
  ~FakeNetworkWrapper() override {}

  http::URLRequest* GetRequest() { return &request_received_; }

  void SetResponse(http::URLResponse response) {
    response_to_return_ = std::move(response);
  }

  // NetworkService:
  void CreateURLLoader(
      fidl::InterfaceRequest<http::URLLoader> loader) override {
    loaders_.push_back(std::make_unique<FakeURLLoader>(
        std::move(loader), std::move(response_to_return_), &request_received_));
  }

 private:
  fidl::Binding<http::HttpService> binding_;
  std::vector<std::unique_ptr<FakeURLLoader>> loaders_;
  http::URLRequest request_received_;
  http::URLResponse response_to_return_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkWrapper);
};

class DestroyWatcher : public fxl::RefCountedThreadSafe<DestroyWatcher> {
 public:
  static fxl::RefPtr<DestroyWatcher> Create(fit::closure callback) {
    return fxl::AdoptRef(new DestroyWatcher(std::move(callback)));
  }

 private:
  explicit DestroyWatcher(fit::closure callback)
      : callback_(std::move(callback)) {}
  ~DestroyWatcher() { callback_(); }

  fit::closure callback_;

  FRIEND_REF_COUNTED_THREAD_SAFE(DestroyWatcher);
};

class NetworkWrapperImplTest : public gtest::TestLoopFixture {
 public:
  NetworkWrapperImplTest()
      : network_service_(dispatcher(), std::make_unique<backoff::TestBackoff>(),
                         [this] { return NewHttpService(); }) {}

  void SetSocketResponse(zx::socket body, uint32_t status_code) {
    http::URLResponse server_response;
    server_response.body = http::URLBody::New();
    server_response.body->set_stream(std::move(body));
    server_response.status_code = status_code;
    http::HttpHeader header;
    header.name = "Location";
    header.value = kRedirectUrl;
    server_response.headers.push_back(std::move(header));
    if (fake_network_service_) {
      fake_network_service_->SetResponse(std::move(server_response));
    } else {
      response_ = fidl::MakeOptional(std::move(server_response));
    }
  }

  void SetStringResponse(const std::string& body, uint32_t status_code) {
    SetSocketResponse(fsl::WriteStringToSocket(body), status_code);
  }

  http::URLRequest NewRequest(const std::string& method,
                              const std::string& url) {
    http::URLRequest request;
    request.method = method;
    request.url = url;
    return request;
  }

 private:
  http::HttpServicePtr NewHttpService() {
    http::HttpServicePtr result;
    fake_network_service_ =
        std::make_unique<FakeNetworkWrapper>(result.NewRequest());
    if (response_) {
      fake_network_service_->SetResponse(std::move(*response_));
    }
    return result;
  }

 protected:
  NetworkWrapperImpl network_service_;
  std::unique_ptr<FakeNetworkWrapper> fake_network_service_;
  http::URLResponsePtr response_;
};

TEST_F(NetworkWrapperImplTest, SimpleRequest) {
  bool callback_destroyed = false;
  http::URLResponse response;
  network_service_.Request(
      [this]() {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, destroy_watcher = DestroyWatcher::Create([&callback_destroyed] {
               callback_destroyed = true;
             }),
       &response](http::URLResponse received_response) {
        response = std::move(received_response);
      });
  EXPECT_FALSE(callback_destroyed);
  RunLoopUntilIdle();

  EXPECT_TRUE(callback_destroyed);
  EXPECT_EQ(200u, response.status_code);
}

TEST_F(NetworkWrapperImplTest, CancelRequest) {
  bool callback_destroyed = false;
  bool received_response = false;
  auto cancel = network_service_.Request(
      [this]() {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &received_response,
       destroy_watcher = DestroyWatcher::Create([this, &callback_destroyed] {
         callback_destroyed = true;
       })](http::URLResponse) { received_response = true; });

  async::PostTask(dispatcher(), [cancel] { cancel->Cancel(); });
  cancel = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(received_response);
  EXPECT_TRUE(callback_destroyed);
}

TEST_F(NetworkWrapperImplTest, NetworkDeleted) {
  int request_count = 0;
  http::URLResponse response;
  network_service_.Request(
      [this, &request_count]() {
        if (request_count == 0) {
          fake_network_service_.reset();
        }
        ++request_count;
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &response](http::URLResponse received_response) {
        response = std::move(received_response);
      });
  RunLoopUntilIdle();

  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response.status_code);
}

TEST_F(NetworkWrapperImplTest, Redirection) {
  int request_count = 0;
  http::URLResponse response;
  network_service_.Request(
      [this, &request_count]() {
        if (request_count == 0) {
          SetStringResponse("Hello", 307);
        } else {
          SetStringResponse("Hello", 200);
        }
        ++request_count;
        return NewRequest("GET", "http://example.com");
      },
      [this, &response](http::URLResponse received_response) {
        response = std::move(received_response);
      });
  RunLoopUntilIdle();

  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response.status_code);
  EXPECT_EQ(kRedirectUrl, fake_network_service_->GetRequest()->url);
}

TEST_F(NetworkWrapperImplTest, CancelOnCallback) {
  fxl::RefPtr<callback::Cancellable> request;
  http::URLResponsePtr response;
  request = network_service_.Request(
      [this] {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &request, &response](http::URLResponse received_response) mutable {
        response = fidl::MakeOptional(std::move(received_response));
        request->Cancel();
        request = nullptr;
      });
  EXPECT_FALSE(response);
  RunLoopUntilIdle();

  EXPECT_TRUE(response);
}

}  // namespace
}  // namespace network_wrapper
