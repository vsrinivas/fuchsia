// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/network/network_service_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <mx/socket.h>

#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/netstack/fidl/net_address.fidl.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace ledger {
namespace {

const char kRedirectUrl[] = "http://example.com/redirect";

// Url loader that stores the url request for inspection in |request_received|,
// and returns response indicated in |response_to_return|. |response_to_return|
// is moved out in ::Start().
class FakeURLLoader : public network::URLLoader {
 public:
  FakeURLLoader(fidl::InterfaceRequest<network::URLLoader> request,
                network::URLResponsePtr response_to_return,
                network::URLRequestPtr* request_received)
      : binding_(this, std::move(request)),
        response_to_return_(std::move(response_to_return)),
        request_received_(request_received) {
    FXL_DCHECK(response_to_return_);
  }
  ~FakeURLLoader() override {}

  // URLLoader:
  void Start(network::URLRequestPtr request,
             const StartCallback& callback) override {
    FXL_DCHECK(response_to_return_);
    *request_received_ = std::move(request);
    callback(std::move(response_to_return_));
  }
  void FollowRedirect(const FollowRedirectCallback& /*callback*/) override {}
  void QueryStatus(const QueryStatusCallback& /*callback*/) override {}

 private:
  fidl::Binding<network::URLLoader> binding_;
  network::URLResponsePtr response_to_return_;
  network::URLRequestPtr* request_received_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeURLLoader);
};

// Fake implementation of network service, allowing to inspect the last request
// passed to any url loader and set the response that url loaders need to
// return. Response is moved out when url request starts, and needs to be set
// each time.
class FakeNetworkService : public network::NetworkService {
 public:
  explicit FakeNetworkService(fidl::InterfaceRequest<NetworkService> request)
      : binding_(this, std::move(request)) {}
  ~FakeNetworkService() override {}

  network::URLRequest* GetRequest() { return request_received_.get(); }

  void SetResponse(network::URLResponsePtr response) {
    response_to_return_ = std::move(response);
  }

  // NetworkService:
  void CreateURLLoader(
      fidl::InterfaceRequest<network::URLLoader> loader) override {
    FXL_DCHECK(response_to_return_);
    loaders_.push_back(std::make_unique<FakeURLLoader>(
        std::move(loader), std::move(response_to_return_), &request_received_));
  }
  void GetCookieStore(mx::channel /*cookie_store*/) override {
    FXL_DCHECK(false);
  }
  void CreateWebSocket(mx::channel /*socket*/) override { FXL_DCHECK(false); }
  void CreateTCPBoundSocket(
      netstack::NetAddressPtr /*local_address*/,
      mx::channel /*bound_socket*/,
      const CreateTCPBoundSocketCallback& /*callback*/) override {
    FXL_DCHECK(false);
  }
  void CreateTCPConnectedSocket(
      netstack::NetAddressPtr /*remote_address*/,
      mx::socket /*send_stream*/,
      mx::socket /*receive_stream*/,
      mx::channel /*client_socket*/,
      const CreateTCPConnectedSocketCallback& /*callback*/) override {
    FXL_DCHECK(false);
  }
  void CreateUDPSocket(mx::channel /*socket*/) override { FXL_DCHECK(false); }
  void CreateHttpServer(netstack::NetAddressPtr /*local_address*/,
                        mx::channel /*delegate*/,
                        const CreateHttpServerCallback& /*callback*/) override {
    FXL_DCHECK(false);
  }
  void RegisterURLLoaderInterceptor(mx::channel /*factory*/) override {
    FXL_DCHECK(false);
  }
  void CreateHostResolver(mx::channel /*host_resolver*/) override {
    FXL_DCHECK(false);
  }

 private:
  fidl::Binding<NetworkService> binding_;
  std::vector<std::unique_ptr<FakeURLLoader>> loaders_;
  network::URLRequestPtr request_received_;
  network::URLResponsePtr response_to_return_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkService);
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

class NetworkServiceImplTest : public test::TestWithMessageLoop {
 public:
  NetworkServiceImplTest()
      : network_service_(message_loop_.task_runner(),
                         [this] { return NewNetworkService(); }) {}

  void SetSocketResponse(mx::socket body, uint32_t status_code) {
    network::URLResponsePtr server_response = network::URLResponse::New();
    server_response->body = network::URLBody::New();
    server_response->body->set_stream(std::move(body));
    server_response->status_code = status_code;
    auto header = network::HttpHeader::New();
    header->name = "Location";
    header->value = kRedirectUrl;
    server_response->headers.push_back(std::move(header));
    if (fake_network_service_) {
      fake_network_service_->SetResponse(std::move(server_response));
    } else {
      response_ = std::move(server_response);
    }
  }

  void SetStringResponse(const std::string& body, uint32_t status_code) {
    SetSocketResponse(fsl::WriteStringToSocket(body), status_code);
  }

  network::URLRequestPtr NewRequest(const std::string& method,
                                    const std::string& url) {
    auto request = network::URLRequest::New();
    request->method = method;
    request->url = url;
    return request;
  }

 private:
  network::NetworkServicePtr NewNetworkService() {
    network::NetworkServicePtr result;
    fake_network_service_ =
        std::make_unique<FakeNetworkService>(result.NewRequest());
    if (response_) {
      fake_network_service_->SetResponse(std::move(response_));
    }
    return result;
  }

 protected:
  NetworkServiceImpl network_service_;
  std::unique_ptr<FakeNetworkService> fake_network_service_;
  network::URLResponsePtr response_;
};

TEST_F(NetworkServiceImplTest, SimpleRequest) {
  bool callback_destroyed = false;
  network::URLResponsePtr response;
  network_service_.Request(
      [this]() {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [
        this, destroy_watcher = DestroyWatcher::Create([&callback_destroyed] {
                callback_destroyed = true;
              }),
        &response
      ](network::URLResponsePtr received_response) {
        response = std::move(received_response);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(callback_destroyed);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(response);
  EXPECT_TRUE(callback_destroyed);
  EXPECT_EQ(200u, response->status_code);
}

TEST_F(NetworkServiceImplTest, CancelRequest) {
  bool callback_destroyed = false;
  bool received_response = false;
  auto cancel = network_service_.Request(
      [this]() {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [
        this, &received_response,
        destroy_watcher = DestroyWatcher::Create([this, &callback_destroyed] {
          callback_destroyed = true;
          message_loop_.PostQuitTask();
        })
      ](network::URLResponsePtr) {
        received_response = true;
        message_loop_.PostQuitTask();
      });

  message_loop_.task_runner()->PostTask([cancel] { cancel->Cancel(); });
  cancel = nullptr;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(received_response);
  EXPECT_TRUE(callback_destroyed);
}

TEST_F(NetworkServiceImplTest, NetworkDeleted) {
  int request_count = 0;
  network::URLResponsePtr response;
  network_service_.Request(
      [this, &request_count]() {
        if (request_count == 0) {
          fake_network_service_.reset();
        }
        ++request_count;
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &response](network::URLResponsePtr received_response) {
        response = std::move(received_response);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(response);
  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response->status_code);
}

TEST_F(NetworkServiceImplTest, Redirection) {
  int request_count = 0;
  network::URLResponsePtr response;
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
      [this, &response](network::URLResponsePtr received_response) {
        response = std::move(received_response);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(response);
  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response->status_code);
  EXPECT_EQ(kRedirectUrl, fake_network_service_->GetRequest()->url);
}

TEST_F(NetworkServiceImplTest, CancelOnCallback) {
  fxl::RefPtr<callback::Cancellable> request;
  network::URLResponsePtr response;
  request = network_service_.Request(
      [this] {
        SetStringResponse("Hello", 200);
        return NewRequest("GET", "http://example.com");
      },
      [this, &request,
       &response](network::URLResponsePtr received_response) mutable {
        response = std::move(received_response);
        message_loop_.PostQuitTask();
        request->Cancel();
        request = nullptr;
      });
  EXPECT_FALSE(response);
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(response);
}

}  // namespace
}  // namespace ledger
