// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/firebase/status.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"
#include "mx/datapipe.h"
#include "mx/vmo.h"

#include <rapidjson/document.h>

namespace cloud_provider {
namespace {

class CloudProviderImplTest : public test::TestWithMessageLoop,
                              public firebase::Firebase,
                              public CommitWatcher {
 public:
  CloudProviderImplTest()
      : cloud_provider_(std::make_unique<CloudProviderImpl>(this)) {}
  ~CloudProviderImplTest() override {}

  // firebase::Firebase:
  void Get(const std::string& key,
           const std::string& query,
           const std::function<void(firebase::Status status,
                                    const rapidjson::Value& value)>& callback)
      override {
    get_keys_.push_back(key);
    get_queries_.push_back(query);
    message_loop_.task_runner()->PostTask([this, callback]() {
      callback(firebase::Status::OK, *get_response_);
      message_loop_.PostQuitTask();
    });
  }

  void Put(
      const std::string& key,
      const std::string& data,
      const std::function<void(firebase::Status status)>& callback) override {
    put_keys_.push_back(key);
    put_data_.push_back(data);
    message_loop_.task_runner()->PostTask([this, callback]() {
      callback(firebase::Status::OK);
      message_loop_.PostQuitTask();
    });
  }

  void Delete(
      const std::string& key,
      const std::function<void(firebase::Status status)>& callback) override {
    // Should never be called.
    FAIL();
  }

  void Watch(const std::string& key,
             const std::string& query,
             firebase::WatchClient* watch_client) override {
    watch_keys_.push_back(key);
    watch_queries_.push_back(query);
    watch_client_ = watch_client;
  }

  void UnWatch(firebase::WatchClient* watch_client) override {
    unwatch_count_++;
    watch_client_ = nullptr;
  }

  // CommitWatcher:
  void OnRemoteCommit(Commit commit, std::string timestamp) override {
    commits_.push_back(std::move(commit));
    server_timestamps_.push_back(std::move(timestamp));
  }

  void OnError() override { commit_watcher_errors_++; }

 protected:
  const std::unique_ptr<CloudProviderImpl> cloud_provider_;

  // These members track calls made by CloudProviderImpl to Firebase client.
  std::vector<std::string> get_keys_;
  std::vector<std::string> get_queries_;
  std::vector<std::string> put_keys_;
  std::vector<std::string> put_data_;
  std::vector<std::string> watch_keys_;
  std::vector<std::string> watch_queries_;
  unsigned int unwatch_count_ = 0u;
  firebase::WatchClient* watch_client_ = nullptr;

  // These members hold response data that Firebase client is to return when
  // called by CloudProviderImpl.
  std::unique_ptr<rapidjson::Document> get_response_;

  // These members track calls received from CloudProviderImpl by this class
  // registered as a CommitWatcher.
  std::vector<Commit> commits_;
  std::vector<std::string> server_timestamps_;
  unsigned int commit_watcher_errors_ = 0u;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
};

TEST_F(CloudProviderImplTest, AddCommit) {
  Commit commit(
      "commit_id", "some_content",
      std::map<ObjectId, Data>{{"object_a", "data_a"}, {"object_b", "data_b"}});

  bool callback_called = false;
  cloud_provider_->AddCommit(commit, [&callback_called](Status status) {
    EXPECT_EQ(Status::OK, status);
    callback_called = true;
  });
  message_loop_.Run();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1u, put_keys_.size());
  EXPECT_EQ(put_keys_.size(), put_data_.size());
  EXPECT_EQ("commits/commit_idV", put_keys_[0]);
  EXPECT_EQ(
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"objects\":{"
      "\"object_aV\":\"data_aV\","
      "\"object_bV\":\"data_bV\"},"
      "\"timestamp\":{\".sv\":\"timestamp\"}"
      "}",
      put_data_[0]);
  EXPECT_TRUE(watch_keys_.empty());
  EXPECT_EQ(0u, unwatch_count_);
}

TEST_F(CloudProviderImplTest, WatchUnwatch) {
  cloud_provider_->WatchCommits("", this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("commits", watch_keys_[0]);
  EXPECT_EQ("", watch_queries_[0]);
  EXPECT_EQ(0u, unwatch_count_);

  cloud_provider_->UnwatchCommits(this);
  EXPECT_EQ(1u, unwatch_count_);
}

TEST_F(CloudProviderImplTest, WatchWithQuery) {
  cloud_provider_->WatchCommits(ServerTimestampToBytes(42), this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("commits", watch_keys_[0]);
  EXPECT_EQ("orderBy=\"timestamp\"&startAt=42", watch_queries_[0]);
}

// Tests handling a server event containing multiple commits.
TEST_F(CloudProviderImplTest, WatchAndGetNotifiedMultiple) {
  cloud_provider_->WatchCommits("", this);

  std::string put_content =
      "{\"id_1V\":"
      "{\"content\":\"some_contentV\","
      "\"id\":\"id_1V\","
      "\"timestamp\":42"
      "},"
      "\"id_2V\":"
      "{\"content\":\"some_other_contentV\","
      "\"id\":\"id_2V\","
      "\"timestamp\":43"
      "}}";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/", document);

  Commit expected_n1("id_1", "some_content", std::map<ObjectId, Data>{});
  Commit expected_n2("id_2", "some_other_content", std::map<ObjectId, Data>{});
  EXPECT_EQ(2u, commits_.size());
  EXPECT_EQ(2u, server_timestamps_.size());
  EXPECT_EQ(expected_n1, commits_[0]);
  EXPECT_EQ(ServerTimestampToBytes(42), server_timestamps_[0]);
  EXPECT_EQ(expected_n2, commits_[1]);
  EXPECT_EQ(ServerTimestampToBytes(43), server_timestamps_[1]);
  EXPECT_EQ(0u, commit_watcher_errors_);
}

// Tests handling a server event containing a single commit.
TEST_F(CloudProviderImplTest, WatchAndGetNotifiedSingle) {
  cloud_provider_->WatchCommits("", this);

  std::string put_content =
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"objects\":{"
      "\"object_aV\":\"data_aV\","
      "\"object_bV\":\"data_bV\"},"
      "\"timestamp\":1472722368296"
      "}";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/commits/commit_idV", document);

  Commit expected_commit(
      "commit_id", "some_content",
      std::map<ObjectId, Data>{{"object_a", "data_a"}, {"object_b", "data_b"}});
  std::string expected_timestamp = ServerTimestampToBytes(1472722368296);
  EXPECT_EQ(1u, commits_.size());
  EXPECT_EQ(expected_commit, commits_[0]);
  EXPECT_EQ(1u, server_timestamps_.size());
  EXPECT_EQ(expected_timestamp, server_timestamps_[0]);
}

// Verifies that the initial response when there is no matching commits is
// ignored.
TEST_F(CloudProviderImplTest, WatchWhenThereIsNothingToWatch) {
  cloud_provider_->WatchCommits("", this);

  std::string put_content = "null";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/", document);
  EXPECT_EQ(0u, commit_watcher_errors_);
  EXPECT_TRUE(commits_.empty());
}

// Verifies that malformed commit notifications are reported through OnError()
// callback and that processing further notifications is stopped.
TEST_F(CloudProviderImplTest, WatchMalformedCommits) {
  rapidjson::Document document;
  EXPECT_EQ(0u, commit_watcher_errors_);
  EXPECT_EQ(0u, unwatch_count_);

  // Not a dictionary.
  document.Parse("[]");
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(1u, commit_watcher_errors_);
  EXPECT_EQ(1u, unwatch_count_);

  // Missing fields.
  document.Parse("{}");
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(2u, commit_watcher_errors_);
  EXPECT_EQ(2u, unwatch_count_);

  // Timestamp is not a number.
  const char content[] =
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"timestamp\":\"42\""
      "}";
  document.Parse(content);
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(3u, commit_watcher_errors_);
}

TEST_F(CloudProviderImplTest, GetCommits) {
  std::string get_response_content =
      "{\"id1V\":"
      "{\"content\":\"xyzV\","
      "\"id\":\"id1V\","
      "\"objects\":{"
      "\"object_aV\":\"aV\","
      "\"object_bV\":\"bV\"},"
      "\"timestamp\":1472722368296"
      "},"
      "\"id2V\":"
      "{\"content\":\"bazingaV\","
      "\"id\":\"id2V\","
      "\"timestamp\":42"
      "}}";
  get_response_ = std::make_unique<rapidjson::Document>();
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  bool callback_called = false;
  auto callback = [&callback_called](Status status,
                                     const std::vector<Record>& records) {
    EXPECT_EQ(Status::OK, status);
    callback_called = true;

    const Commit expected_commit_1(
        "id1", "xyz",
        std::map<ObjectId, Data>{{"object_a", "a"}, {"object_b", "b"}});
    const Commit expected_commit_2("id2", "bazinga",
                                   std::map<ObjectId, Data>{});
    EXPECT_EQ(2u, records.size());
    // Verify that commits are ordered by timestamp.
    EXPECT_EQ(expected_commit_2, records[0].commit);
    EXPECT_EQ(ServerTimestampToBytes(42), records[0].timestamp);
    EXPECT_EQ(expected_commit_1, records[1].commit);
    EXPECT_EQ(ServerTimestampToBytes(1472722368296), records[1].timestamp);
  };

  cloud_provider_->GetCommits(ServerTimestampToBytes(42), callback);
  message_loop_.Run();

  EXPECT_EQ(1u, get_keys_.size());
  EXPECT_EQ(1u, get_queries_.size());
  EXPECT_EQ("commits", get_keys_[0]);
  EXPECT_EQ("orderBy=\"timestamp\"&startAt=42", get_queries_[0]);
  EXPECT_TRUE(callback_called);
}

TEST_F(CloudProviderImplTest, GetCommitsWhenThereAreNone) {
  std::string get_response_content = "null";
  get_response_ = std::make_unique<rapidjson::Document>();
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  Status status;
  std::vector<Record> records;
  cloud_provider_->GetCommits(
      ServerTimestampToBytes(42),
      test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                    &records));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(records.empty());
}

TEST_F(CloudProviderImplTest, AddObject) {
  mx::vmo data;
  ASSERT_TRUE(mtl::VmoFromString("bazinga", &data));

  bool callback_called = false;
  cloud_provider_->AddObject("object_id", std::move(data),
                             [&callback_called](Status status) {
                               EXPECT_EQ(Status::OK, status);
                               callback_called = true;
                             });
  message_loop_.Run();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1u, put_keys_.size());
  EXPECT_EQ(put_keys_.size(), put_data_.size());
  EXPECT_EQ("objects/object_idV", put_keys_[0]);
  EXPECT_EQ("\"bazingaV\"", put_data_[0]);
}

TEST_F(CloudProviderImplTest, GetObject) {
  std::string get_response_content = "\"bazingaV\"";
  get_response_.reset(new rapidjson::Document());
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  bool callback_called = false;
  cloud_provider_->GetObject(
      "object_id", [&callback_called](Status status, uint64_t size,
                                      mx::datapipe_consumer data) {
        EXPECT_EQ(Status::OK, status);
        std::string data_str;
        EXPECT_TRUE(mtl::BlockingCopyToString(std::move(data), &data_str));
        EXPECT_EQ("bazinga", data_str);
        EXPECT_EQ(7u, data_str.size());
        EXPECT_EQ(7u, size);
        callback_called = true;
      });
  message_loop_.Run();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1u, get_keys_.size());
  EXPECT_EQ("objects/object_idV", get_keys_[0]);
}

TEST_F(CloudProviderImplTest, GetObjectNotFound) {
  std::string get_response_content = "null";
  get_response_.reset(new rapidjson::Document());
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  Status status;
  uint64_t size;
  mx::datapipe_consumer data;
  cloud_provider_->GetObject(
      "object_id", test::Capture([this] { message_loop_.PostQuitTask(); },
                                 &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(0u, size);
}

}  // namespace
}  // namespace cloud_provider
