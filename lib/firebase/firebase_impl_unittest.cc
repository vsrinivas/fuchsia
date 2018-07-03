// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/firebase_impl.h"

#include <memory>
#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/network_wrapper/fake_network_wrapper.h>
#include <lib/network_wrapper/network_wrapper_impl.h>
#include <rapidjson/document.h>

#include "peridot/lib/socket/socket_pair.h"

namespace firebase {
namespace {

class FirebaseImplTest : public gtest::TestLoopFixture, public WatchClient {
 public:
  FirebaseImplTest()
      : fake_network_wrapper_(dispatcher()),
        firebase_(&fake_network_wrapper_, "example", "pre/fix") {}
  ~FirebaseImplTest() override {}

 protected:
  // WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override {
    put_count_++;
    put_paths_.push_back(path);
    put_data_.emplace_back(value, document_.GetAllocator());
  }

  void OnPatch(const std::string& path,
               const rapidjson::Value& value) override {
    patch_count_++;
    patch_paths_.push_back(path);
    patch_data_.emplace_back(value, document_.GetAllocator());
  }

  void OnCancel() override { cancel_count_++; }

  void OnAuthRevoked(const std::string& reason) override {
    auth_revoked_count_++;
    auth_revoked_reasons_.push_back(reason);
  }

  void OnMalformedEvent() override { malformed_event_count_++; }

  void OnConnectionError() override { connection_error_count_++; }

  std::vector<std::string> put_paths_;
  std::vector<rapidjson::Value> put_data_;
  unsigned int put_count_ = 0u;

  std::vector<std::string> patch_paths_;
  std::vector<rapidjson::Value> patch_data_;
  unsigned int patch_count_ = 0u;

  unsigned int cancel_count_ = 0u;

  std::vector<std::string> auth_revoked_reasons_;
  unsigned int auth_revoked_count_ = 0u;

  unsigned int malformed_event_count_ = 0u;

  unsigned int connection_error_count_ = 0u;

  network_wrapper::FakeNetworkWrapper fake_network_wrapper_;
  FirebaseImpl firebase_;

 private:
  // Used for its allocator which we use to make copies of rapidjson Values.
  rapidjson::Document document_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FirebaseImplTest);
};

// Verifies that GET requests are handled correctly.
TEST_F(FirebaseImplTest, Get) {
  fake_network_wrapper_.SetStringResponse("\"content\"", 200);

  bool called;
  Status status;
  std::unique_ptr<rapidjson::Value> value;
  firebase_.Get(
      "bazinga", {},
      callback::Capture(callback::SetWhenCalled(&called), &status, &value));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_TRUE(value);
  EXPECT_TRUE(value->IsString());
  EXPECT_EQ("content", *value);
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/bazinga.json",
            fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
}

TEST_F(FirebaseImplTest, GetError) {
  fake_network_wrapper_.SetStringResponse("\"content\"", 404);
  bool called;
  Status status;
  std::unique_ptr<rapidjson::Value> value;
  firebase_.Get(
      "bazinga", {},
      callback::Capture(callback::SetWhenCalled(&called), &status, &value));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_NE(Status::OK, status);
  ASSERT_TRUE(value);
  EXPECT_TRUE(value->IsNull());
}

TEST_F(FirebaseImplTest, GetWithSingleQueryParam) {
  fake_network_wrapper_.SetStringResponse("content", 200);
  bool called;
  Status status;
  std::unique_ptr<rapidjson::Value> value;
  firebase_.Get("bazinga", {"orderBy=\"timestamp\""},
                callback::Capture(
                    callback::SetWhenCalled(&called), &status,
                    static_cast<std::unique_ptr<rapidjson::Value>*>(nullptr)));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARSE_ERROR, status);
  EXPECT_EQ(
      "https://example.firebaseio.com/pre/fix/"
      "bazinga.json?orderBy=\"timestamp\"",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
}

TEST_F(FirebaseImplTest, GetWithTwoQueryParams) {
  fake_network_wrapper_.SetStringResponse("content", 200);
  bool called;
  Status status;
  firebase_.Get("bazinga", {"one_param", "other_param=bla"},
                callback::Capture(
                    callback::SetWhenCalled(&called), &status,
                    static_cast<std::unique_ptr<rapidjson::Value>*>(nullptr)));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PARSE_ERROR, status);
  EXPECT_EQ(
      "https://example.firebaseio.com/pre/fix/"
      "bazinga.json?one_param&other_param=bla",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
}

// Verifies that request urls for root of the db are correctly formed.
TEST_F(FirebaseImplTest, Root) {
  fake_network_wrapper_.SetStringResponse("42", 200);
  bool called;
  Status status;
  firebase_.Get("", {},
                callback::Capture(
                    callback::SetWhenCalled(&called), &status,
                    static_cast<std::unique_ptr<rapidjson::Value>*>(nullptr)));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/.json",
            fake_network_wrapper_.GetRequest()->url);
}

// Verifies that PUT requests are handled correctly.
TEST_F(FirebaseImplTest, Put) {
  // Firebase server seems to respond with the data we sent to it. This is not
  // useful for the client so our API doesn't expose it to the client.
  fake_network_wrapper_.SetStringResponse("\"Alice\"", 200);
  bool called;
  Status status;
  firebase_.Put("name", {}, "\"Alice\"",
                callback::Capture(callback::SetWhenCalled(&called), &status));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/name.json",
            fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("PUT", fake_network_wrapper_.GetRequest()->method);
}

// Verifies that PATCH requests are handled correctly.
TEST_F(FirebaseImplTest, Patch) {
  fake_network_wrapper_.SetStringResponse("\"ok\"", 200);
  bool called;
  Status status;
  std::string data = R"({"name":"Alice"})";
  firebase_.Patch("person", {}, data,
                  callback::Capture(callback::SetWhenCalled(&called), &status));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/person.json",
            fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("PATCH", fake_network_wrapper_.GetRequest()->method);
}

// Verifies that DELETE requests are made correctly.
TEST_F(FirebaseImplTest, Delete) {
  fake_network_wrapper_.SetStringResponse("", 200);
  bool called;
  Status status;
  firebase_.Delete(
      "name", {}, callback::Capture(callback::SetWhenCalled(&called), &status));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ("https://example.firebaseio.com/pre/fix/name.json",
            fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("DELETE", fake_network_wrapper_.GetRequest()->method);
}

// Verifies that event-stream requests are correctly formed.
TEST_F(FirebaseImplTest, WatchRequest) {
  fake_network_wrapper_.SetStringResponse("", 200);

  firebase_.Watch("some/path", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ("https://example.firebaseio.com/pre/fix/some/path.json",
            fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
  EXPECT_EQ(1u, fake_network_wrapper_.GetRequest()->headers->size());
  EXPECT_EQ("Accept", fake_network_wrapper_.GetRequest()->headers->at(0).name);
  EXPECT_EQ("text/event-stream",
            fake_network_wrapper_.GetRequest()->headers->at(0).value);
}

TEST_F(FirebaseImplTest, WatchRequestWithQuery) {
  fake_network_wrapper_.SetStringResponse("", 200);

  firebase_.Watch("some/path", {"orderBy=\"timestamp\""}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(
      "https://example.firebaseio.com/pre/fix/some/path.json"
      "?orderBy=\"timestamp\"",
      fake_network_wrapper_.GetRequest()->url);
  EXPECT_EQ("GET", fake_network_wrapper_.GetRequest()->method);
  EXPECT_EQ(1u, fake_network_wrapper_.GetRequest()->headers->size());
  EXPECT_EQ("Accept", fake_network_wrapper_.GetRequest()->headers->at(0).name);
  EXPECT_EQ("text/event-stream",
            fake_network_wrapper_.GetRequest()->headers->at(0).value);
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
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(3u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);

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
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(1u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);

  EXPECT_EQ("/bla/", patch_paths_[0]);
  EXPECT_EQ("Alice", patch_data_[0]["name1"]);
  EXPECT_EQ("Bob", patch_data_[0]["name2"]);
}

TEST_F(FirebaseImplTest, WatchKeepAlive) {
  std::string stream_body = std::string(
      "event: keep-alive\n"
      "data: null\n"
      "\n");
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("name", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
}

TEST_F(FirebaseImplTest, WatchCancel) {
  std::string stream_body = std::string(
      "event: cancel\n"
      "data: null\n"
      "\n");
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(1u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
}

TEST_F(FirebaseImplTest, WatchAuthRevoked) {
  std::string stream_body = std::string(
      "event: auth_revoked\n"
      "data: credential is no longer valid\n"
      "\n");
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(1u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);

  EXPECT_EQ("credential is no longer valid", auth_revoked_reasons_[0]);
}

TEST_F(FirebaseImplTest, WatchErrorUnknownEvent) {
  std::string stream_body = std::string(
      "event: wild-animal-appears\n"
      "data: null\n"
      "\n");
  fake_network_wrapper_.SetStringResponse(stream_body, 200);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(1u, malformed_event_count_);
}

TEST_F(FirebaseImplTest, WatchHttpError) {
  fake_network_wrapper_.SetStringResponse("", 404);

  firebase_.Watch("/", {}, this);
  RunLoopUntilIdle();

  EXPECT_EQ(0u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
  EXPECT_EQ(1u, connection_error_count_);
}

TEST_F(FirebaseImplTest, UnWatch) {
  std::string event = std::string(
      "event: put\n"
      "data: {\"path\":\"/\",\"data\":\"Alice\"}\n"
      "\n");
  socket::SocketPair socket;
  fake_network_wrapper_.SetSocketResponse(std::move(socket.socket1), 200);
  firebase_.Watch("/", {}, this);

  EXPECT_TRUE(fsl::BlockingCopyFromString(event, socket.socket2));
  RunLoopUntilIdle();

  EXPECT_EQ(1u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
  EXPECT_EQ(0u, connection_error_count_);

  EXPECT_TRUE(fsl::BlockingCopyFromString(event, socket.socket2));
  RunLoopUntilIdle();

  EXPECT_EQ(2u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
  EXPECT_EQ(0u, connection_error_count_);

  // Unregister the watch client and make sure that we are *not* notified about
  // the next event.
  firebase_.UnWatch(this);
  EXPECT_TRUE(fsl::BlockingCopyFromString(event, socket.socket2));
  RunLoopUntilIdle();

  EXPECT_EQ(2u, put_count_);
  EXPECT_EQ(0u, patch_count_);
  EXPECT_EQ(0u, cancel_count_);
  EXPECT_EQ(0u, auth_revoked_count_);
  EXPECT_EQ(0u, malformed_event_count_);
  EXPECT_EQ(0u, connection_error_count_);
}

}  // namespace
}  // namespace firebase
