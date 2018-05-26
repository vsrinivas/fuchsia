// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/network_wrapper/network_wrapper_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <netstack/cpp/fidl.h>
#include <network/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/socket.h>

#include "gtest/gtest.h"
#include "lib/backoff/testing/test_backoff.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_with_message_loop.h"

namespace network_wrapper {
namespace {

const char kRedirectUrl[] = "http://example.com/redirect";

// Url loader that stores the url request for inspection in |request_received|,
// and returns response indicated in |response_to_return|. |response_to_return|
// is moved out in ::Start().
class FakeURLLoader : public network::URLLoader {
 public:
  FakeURLLoader(fidl::InterfaceRequest<network::URLLoader> request,
                network::URLResponse response_to_return,
                network::URLRequest* request_received)
      : binding_(this, std::move(request)),
        response_to_return_(std::move(response_to_return)),
        request_received_(request_received) {}
  ~FakeURLLoader() override {}

  // URLLoader:
  void Start(network::URLRequest request, StartCallback callback) override {
    *request_received_ = std::move(request);
    callback(std::move(response_to_return_));
  }
  void FollowRedirect(FollowRedirectCallback) override {}
  void QueryStatus(QueryStatusCallback) override {}

 private:
  fidl::Binding<network::URLLoader> binding_;
  network::URLResponse response_to_return_;
  network::URLRequest* request_received_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeURLLoader);
};

// Fake implementation of network service, allowing to inspect the last request
// passed to any url loader and set the response that url loaders need to
// return. Response is moved out when url request starts, and needs to be set
// each time.
class FakeNetworkWrapper : public network::NetworkService {
 public:
  explicit FakeNetworkWrapper(fidl::InterfaceRequest<NetworkService> request)
      : binding_(this, std::move(request)) {}
  ~FakeNetworkWrapper() override {}

  network::URLRequest* GetRequest() { return &request_received_; }

  void SetResponse(network::URLResponse response) {
    response_to_return_ = std::move(response);
  }

  // NetworkService:
  void CreateURLLoader(
      fidl::InterfaceRequest<network::URLLoader> loader) override {
    loaders_.push_back(std::make_unique<FakeURLLoader>(
        std::move(loader), std::move(response_to_return_), &request_received_));
  }
  void GetCookieStore(zx::channel /*cookie_store*/) override {
    FXL_DCHECK(false);
  }
  void CreateWebSocket(zx::channel /*socket*/) override { FXL_DCHECK(false); }

 private:
  fidl::Binding<NetworkService> binding_;
  std::vector<std::unique_ptr<FakeURLLoader>> loaders_;
  network::URLRequest request_received_;
  network::URLResponse response_to_return_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkWrapper);
};

class DestroyWatcher : public fxl::RefCountedThreadSafe<DestroyWatcher> {
 public:
  static fxl::RefPtr<DestroyWatcher> Create(const fxl::Closure& callback) {
    return fxl::AdoptRef(new DestroyWatcher(callback));
  }

 private:
  explicit DestroyWatcher(fxl::Closure callback)
      : callback_(std::move(callback)) {}
  ~DestroyWatcher() { callback_(); }

  fxl::Closure callback_;

  FRIEND_REF_COUNTED_THREAD_SAFE(DestroyWatcher);
};

class NetworkWrapperImplTest : public gtest::TestWithMessageLoop {
 public:
  NetworkWrapperImplTest()
      : network_service_(message_loop_.async(),
                         std::make_unique<backoff::TestBackoff>(),
                         [this] { return NewNetworkService(); }) {}

  void SetSocketResponse(zx::socket body, uint32_t status_code) {
    network::URLResponse server_response;
    server_response.body = network::URLBody::New();
    server_response.body->set_stream(std::move(body));
    server_response.status_code = status_code;
    network::HttpHeader header;
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

  network::URLRequest NewRequest(const std::string& method,
                                 const std::string& url) {
    network::URLRequest request;
    request.method = method;
    request.url = url;
    return request;
  }

 private:
  network::NetworkServicePtr NewNetworkService() {
    network::NetworkServicePtr result;
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
  network::URLResponsePtr response_;
};

TEST_F(NetworkWrapperImplTest, SimpleRequest) {
  bool callback_destroyed = false;
  network::URLResponse response;
  network_service_.Request(
      [this]() {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, destroy_watcher = DestroyWatcher::Create([&callback_destroyed] {
               callback_destroyed = true;
             }),
       &response](network::URLResponse received_response) {
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
       })](network::URLResponse) { received_response = true; });

  async::PostTask(message_loop_.async(), [cancel] { cancel->Cancel(); });
  cancel = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(received_response);
  EXPECT_TRUE(callback_destroyed);
}

TEST_F(NetworkWrapperImplTest, NetworkDeleted) {
  int request_count = 0;
  network::URLResponse response;
  network_service_.Request(
      [this, &request_count]() {
        if (request_count == 0) {
          fake_network_service_.reset();
        }
        ++request_count;
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &response](network::URLResponse received_response) {
        response = std::move(received_response);
      });
  RunLoopUntilIdle();

  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response.status_code);
}

TEST_F(NetworkWrapperImplTest, Redirection) {
  int request_count = 0;
  network::URLResponse response;
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
      [this, &response](network::URLResponse received_response) {
        response = std::move(received_response);
      });
  RunLoopUntilIdle();

  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response.status_code);
  EXPECT_EQ(kRedirectUrl, fake_network_service_->GetRequest()->url);
}

TEST_F(NetworkWrapperImplTest, CancelOnCallback) {
  fxl::RefPtr<callback::Cancellable> request;
  network::URLResponsePtr response;
  request = network_service_.Request(
      [this] {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &request,
       &response](network::URLResponse received_response) mutable {
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
