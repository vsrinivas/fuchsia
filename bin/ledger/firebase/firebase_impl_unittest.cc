// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/firebase/firebase_impl.h"

#include <memory>
#include <utility>

#include <rapidjson/document.h>

#include "apps/ledger/src/glue/data_pipe/data_pipe.h"
#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/network/services/network_service.fidl.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace firebase {
namespace {

class FirebaseImplTest : public ::testing::Test, public WatchClient {
 public:
  FirebaseImplTest()
      : fake_network_service_(message_loop_.task_runner()),
        firebase_(&fake_network_service_, "example", "pre/fix") {}
  ~FirebaseImplTest() override {}

 protected:
  // Allows to step through the watch events one by one. Needs to be called each
  // time before starting the message loop.
  void QuitLoopOnNextEvent() { quit_loop_on_next_event_ = true; }

  void RunLoopWithTimeout() {
    message_loop_.task_runner()->PostDelayedTask(
        [this] {
          message_loop_.PostQuitTask();
          FAIL();
        },
        ftl::TimeDelta::FromSeconds(1));
    message_loop_.Run();
  }

  // WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override {
    put_count_++;
    put_paths_.push_back(path);
    put_data_.push_back(rapidjson::Value(value, document_.GetAllocator()));
    QuitLoopIfNeeded();
  }

  void OnPatch(const std::string& path,
               const rapidjson::Value& value) override {
    patch_count_++;
    patch_paths_.push_back(path);
    patch_data_.push_back(rapidjson::Value(value, document_.GetAllocator()));
    QuitLoopIfNeeded();
  }

  void OnCancel() override {
    cancel_count_++;
    QuitLoopIfNeeded();
  }

  void OnAuthRevoked(const std::string& reason) override {
    auth_revoked_count_++;
    auth_revoked_reasons_.push_back(reason);
    QuitLoopIfNeeded();
  }

  void OnError() override { error_count_++; }

  void OnDone() override { message_loop_.PostQuitTask(); }

  void SetPipeResponse(mx::datapipe_consumer body, uint32_t status_code) {
    network::URLResponsePtr server_response = network::URLResponse::New();
    server_response->body = network::URLBody::New();
    server_response->body->set_stream(std::move(body));
    server_response->status_code = status_code;
    fake_network_service_.SetResponse(std::move(server_response));
  }

  void SetStringResponse(const std::string& body, uint32_t status_code) {
    SetPipeResponse(mtl::WriteStringToConsumerHandle(body), status_code);
  }

  std::vector<std::string> put_paths_;
  std::vector<rapidjson::Value> put_data_;
  unsigned int put_count_ = 0u;

  std::vector<std::string> patch_paths_;
  std::vector<rapidjson::Value> patch_data_;
  unsigned int patch_count_ = 0u;

  unsigned int cancel_count_ = 0u;

  std::vector<std::string> auth_revoked_reasons_;
  unsigned int auth_revoked_count_ = 0u;

  unsigned int error_count_ = 0u;

  mtl::MessageLoop message_loop_;
  ledger::FakeNetworkService fake_network_service_;
  FirebaseImpl firebase_;

 private:
  void QuitLoopIfNeeded() {
    if (quit_loop_on_next_event_) {
      message_loop_.PostQuitTask();
      quit_loop_on_next_event_ = false;
    }
  }

  bool quit_loop_on_next_event_ = false;

  // Used for its allocator which we use to make copies of rapidjson Values.
  rapidjson::Document document_;
  FTL_DISALLOW_COPY_AND_ASSIGN(FirebaseImplTest);
};

// Verifies that GET requests are handled correctly.
TEST_F(FirebaseImplTest, Get) {
  SetStringResponse("\"content\"", 200);
  firebase_.Get("bazinga", "",
                [this](Status status, const rapidjson::Value& value) {
                  std::string response_string;
                  EXPECT_EQ(Status::OK, status);
                  EXPECT_TRUE(value.IsString());
                  EXPECT_EQ("content", value);
                  message_loop_.PostQuitTask();
                });

  RunLoopWithTimeout();
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/bazinga.json",
            fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);
}

TEST_F(FirebaseImplTest, GetError) {
  SetStringResponse("\"content\"", 404);
  firebase_.Get("bazinga", "",
                [this](Status status, const rapidjson::Value& value) {
                  std::string response_string;
                  EXPECT_NE(Status::OK, status);
                  EXPECT_TRUE(value.IsNull());
                  message_loop_.PostQuitTask();
                });

  RunLoopWithTimeout();
}

TEST_F(FirebaseImplTest, GetWithQuery) {
  SetStringResponse("content", 200);
  firebase_.Get("bazinga", "orderBy=\"timestamp\"",
                [this](Status status, const rapidjson::Value& value) {
                  message_loop_.PostQuitTask();
                });

  RunLoopWithTimeout();
  EXPECT_EQ(
      "https://example.firebaseio.com/pre/fix/"
      "bazinga.json?orderBy=\"timestamp\"",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);
}

// Verifies that request urls for root of the db are correctly formed.
TEST_F(FirebaseImplTest, Root) {
  SetStringResponse("42", 200);
  firebase_.Get("", "", [this](Status status, const rapidjson::Value& value) {
    message_loop_.PostQuitTask();
  });

  RunLoopWithTimeout();
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/.json",
            fake_network_service_.GetRequest()->url);
}

// Verifies that PUT requests are handled correctly.
TEST_F(FirebaseImplTest, Put) {
  // Firebase server seems to respond with the data we sent to it. This is not
  // useful for the client so our API doesn't expose it to the client.
  SetStringResponse("\"Alice\"", 200);
  firebase_.Put("name", "\"Alice\"", [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });

  RunLoopWithTimeout();
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/name.json",
            fake_network_service_.GetRequest()->url);
  EXPECT_EQ("PUT", fake_network_service_.GetRequest()->method);
}

// Verifies that DELETE requests are made correctly.
TEST_F(FirebaseImplTest, Delete) {
  SetStringResponse("", 200);
  firebase_.Delete("name", [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });

  RunLoopWithTimeout();
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/name.json",
            fake_network_service_.GetRequest()->url);
  EXPECT_EQ("DELETE", fake_network_service_.GetRequest()->method);
}

// Verifies that event-stream requests are correctly formed.
TEST_F(FirebaseImplTest, WatchRequest) {
  SetStringResponse("", 200);

  firebase_.Watch("some/path", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ("https://example.firebaseio.com/pre/fix/some/path.json",
            fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);
  EXPECT_EQ(1u, fake_network_service_.GetRequest()->headers.size());
  EXPECT_EQ("Accept", fake_network_service_.GetRequest()->headers[0]->name);
  EXPECT_EQ("text/event-stream",
            fake_network_service_.GetRequest()->headers[0]->value);
}

TEST_F(FirebaseImplTest, WatchRequestWithQuery) {
  SetStringResponse("", 200);

  firebase_.Watch("some/path", "orderBy=\"timestamp\"", this);
  RunLoopWithTimeout();

  EXPECT_EQ(
      "https://example.firebaseio.com/pre/fix/some/path.json"
      "?orderBy=\"timestamp\"",
      fake_network_service_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_service_.GetRequest()->method);
  EXPECT_EQ(1u, fake_network_service_.GetRequest()->headers.size());
  EXPECT_EQ("Accept", fake_network_service_.GetRequest()->headers[0]->name);
  EXPECT_EQ("text/event-stream",
            fake_network_service_.GetRequest()->headers[0]->value);
}

TEST_F(FirebaseImplTest, WatchPut) {
  std::string stream_body = std::string(
      "event: put\n"
      "data: {\"path\":\"/\",\"data\":\"Alice\"}\n"
      "\n"
      "event: put\n"
      "data: {\"path\":\"/bla/\",\"data\":{\"name\":\"Bob\"}}\n"
      "\n"
      "event: put\n"
      "data: {\"path\":\"/\",\"data\":42.5}\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(3u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);

  EXPECT_EQ("/", put_paths_[0]);
  EXPECT_EQ("Alice", put_data_[0]);

  EXPECT_EQ("/bla/", put_paths_[1]);
  EXPECT_EQ("Bob", put_data_[1]["name"]);

  EXPECT_EQ("/", put_paths_[2]);
  EXPECT_EQ(42.5, put_data_[2]);
}

TEST_F(FirebaseImplTest, WatchPatch) {
  std::string stream_body = std::string(
      "event: patch\n"
      "data: "
      "{\"path\":\"/bla/\",\"data\":{\"name1\":\"Alice\",\"name2\":\"Bob\"}}\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(1u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);

  EXPECT_EQ("/bla/", patch_paths_[0]);
  EXPECT_EQ("Alice", patch_data_[0]["name1"]);
  EXPECT_EQ("Bob", patch_data_[0]["name2"]);
}

TEST_F(FirebaseImplTest, WatchKeepAlive) {
  std::string stream_body = std::string(
      "event: keep-alive\n"
      "data: null\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("name", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);
}

TEST_F(FirebaseImplTest, WatchCancel) {
  std::string stream_body = std::string(
      "event: cancel\n"
      "data: null\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(1u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);
}

TEST_F(FirebaseImplTest, WatchAuthRevoked) {
  std::string stream_body = std::string(
      "event: auth_revoked\n"
      "data: \"bazinga!\"\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(1u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);

  EXPECT_EQ("bazinga!", auth_revoked_reasons_[0]);
}

TEST_F(FirebaseImplTest, WatchErrorUnknownEvent) {
  std::string stream_body = std::string(
      "event: wild-animal-appears\n"
      "data: null\n"
      "\n");
  SetStringResponse(stream_body, 200);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(1u, error_count_);
}

TEST_F(FirebaseImplTest, WatchHttpError) {
  SetStringResponse("", 404);

  firebase_.Watch("/", "", this);
  RunLoopWithTimeout();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(1u, error_count_);
}

TEST_F(FirebaseImplTest, UnWatch) {
  std::string event = std::string(
      "event: put\n"
      "data: {\"path\":\"/\",\"data\":\"Alice\"}\n"
      "\n");
  glue::DataPipe data_pipe;
  SetPipeResponse(std::move(data_pipe.consumer_handle), 200);
  firebase_.Watch("/", "", this);

  EXPECT_TRUE(mtl::BlockingCopyFromString(event, data_pipe.producer_handle));
  QuitLoopOnNextEvent();
  RunLoopWithTimeout();

  EXPECT_EQ(1u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);

  EXPECT_TRUE(mtl::BlockingCopyFromString(event, data_pipe.producer_handle));
  QuitLoopOnNextEvent();
  RunLoopWithTimeout();

  EXPECT_EQ(2u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);

  // Unregister the watch client and make sure that we are not notified about
  // the next event.
  firebase_.UnWatch(this);
  EXPECT_TRUE(mtl::BlockingCopyFromString(event, data_pipe.producer_handle));
  QuitLoopOnNextEvent();

  // TODO(ppi): how to avoid the wait?
  message_loop_.task_runner()->PostDelayedTask(
      [this] { message_loop_.PostQuitTask(); },
      ftl::TimeDelta::FromMilliseconds(100));
  RunLoopWithTimeout();

  EXPECT_EQ(2u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, error_count_);
}

}  // namespace
}  // namespace firebase
