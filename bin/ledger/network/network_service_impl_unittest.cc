// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/network/network_service_impl.h"

#include <mx/datapipe.h>

#include "apps/ledger/src/fake_network_service/fake_network_service.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {

const char kRedirectUrl[] = "http://example.com/redirect";

class DestroyWatcher : public ftl::RefCountedThreadSafe<DestroyWatcher> {
 public:
  static ftl::RefPtr<DestroyWatcher> Create(const ftl::Closure& callback) {
    return ftl::AdoptRef(new DestroyWatcher(callback));
  }

 private:
  DestroyWatcher(const ftl::Closure& callback) : callback_(callback) {}
  ~DestroyWatcher() { callback_(); }

  ftl::Closure callback_;

  FRIEND_REF_COUNTED_THREAD_SAFE(DestroyWatcher);
};

class NetworkServiceImplTest : public ::testing::Test {
 public:
  NetworkServiceImplTest()
      : network_service_([this] { return NewNetworkService(); }) {}

  void SetPipeResponse(mx::datapipe_consumer body, uint32_t status_code) {
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
    SetPipeResponse(mtl::WriteStringToConsumerHandle(body), status_code);
  }

  network::URLRequestPtr NewRequest(const std::string& method,
                                    const std::string& url) {
    auto request = network::URLRequest::New();
    request->method = method;
    request->url = url;
    return request;
  }

  void RunLoop() {
    loop_.task_runner()->PostDelayedTask(
        [this] {
          loop_.PostQuitTask();
          FAIL();
        },
        ftl::TimeDelta::FromSeconds(1));
    loop_.Run();
  }

 private:
  network::NetworkServicePtr NewNetworkService() {
    network::NetworkServicePtr result;
    fake_network_service_ =
        std::make_unique<fake_network_service::FakeNetworkService>(
            GetProxy(&result));
    if (response_) {
      fake_network_service_->SetResponse(std::move(response_));
    }
    return result;
  }

 protected:
  mtl::MessageLoop loop_;
  NetworkServiceImpl network_service_;
  std::unique_ptr<fake_network_service::FakeNetworkService>
      fake_network_service_;
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
        this, destroy_watcher = DestroyWatcher::Create(
                  [&callback_destroyed] { callback_destroyed = true; }),
        &response
      ](network::URLResponsePtr received_response) {
        response = std::move(received_response);
        loop_.PostQuitTask();
      });
  EXPECT_FALSE(callback_destroyed);
  RunLoop();

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
          loop_.PostQuitTask();
        })
      ](network::URLResponsePtr) {
        received_response = true;
        loop_.PostQuitTask();
      });

  loop_.task_runner()->PostTask([cancel] { cancel->Cancel(); });
  cancel = nullptr;
  RunLoop();
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
        loop_.PostQuitTask();
      });
  RunLoop();

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
        loop_.PostQuitTask();
      });
  RunLoop();

  EXPECT_TRUE(response);
  EXPECT_EQ(2, request_count);
  EXPECT_EQ(200u, response->status_code);
  EXPECT_EQ(kRedirectUrl, fake_network_service_->GetRequest()->url);
}

}  // namespace
}  // namespace ledger
