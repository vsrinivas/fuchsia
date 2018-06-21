// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/page_cloud_handler_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>
#include <rapidjson/document.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"
#include "peridot/lib/firebase/encoding.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase/status.h"

namespace cloud_provider_firebase {
namespace {

class PageCloudHandlerImplTest : public gtest::TestWithLoop,
                                 public gcs::CloudStorage,
                                 public firebase::Firebase,
                                 public CommitWatcher {
 public:
  PageCloudHandlerImplTest()
      : cloud_provider_(std::make_unique<PageCloudHandlerImpl>(this, this)) {}
  ~PageCloudHandlerImplTest() override {}

  // gcs::CloudStorage:
  void UploadObject(std::string auth_token, const std::string& key,
                    fsl::SizedVmo data,
                    std::function<void(gcs::Status)> callback) override {
    upload_auth_tokens_.push_back(std::move(auth_token));
    upload_keys_.push_back(key);
    upload_data_.push_back(std::move(data));
    async::PostTask(dispatcher(), [callback = std::move(callback)] {
      callback(gcs::Status::OK);
    });
  }

  void DownloadObject(
      std::string auth_token, const std::string& key,
      std::function<void(gcs::Status status, uint64_t size, zx::socket data)>
          callback) override {
    download_auth_tokens_.push_back(std::move(auth_token));
    download_keys_.push_back(key);
    async::PostTask(dispatcher(), [this, callback = std::move(callback)] {
      callback(download_status_, download_response_size_,
               std::move(download_response_));
    });
  }

  // firebase::Firebase:
  void Get(const std::string& key, const std::vector<std::string>& query_params,
           std::function<void(firebase::Status status,
                              const rapidjson::Value& value)>
               callback) override {
    get_keys_.push_back(key);
    get_queries_.push_back(query_params);
    async::PostTask(dispatcher(), [this, callback = std::move(callback)] {
      callback(firebase::Status::OK, *get_response_);
    });
  }

  void Put(const std::string& key,
           const std::vector<std::string>& /*query_params*/,
           const std::string& data,
           std::function<void(firebase::Status status)> callback) override {
    put_keys_.push_back(key);
    put_data_.push_back(data);
    async::PostTask(dispatcher(), [callback = std::move(callback)] {
      callback(firebase::Status::OK);
    });
  }

  void Patch(const std::string& key,
             const std::vector<std::string>& query_params,
             const std::string& data,
             std::function<void(firebase::Status status)> callback) override {
    patch_keys_.push_back(key);
    patch_queries_.push_back(query_params);
    patch_data_.push_back(data);
    async::PostTask(dispatcher(), [callback = std::move(callback)] {
      callback(firebase::Status::OK);
    });
  }

  void Delete(
      const std::string& /*key*/,
      const std::vector<std::string>& /*query_params*/,
      std::function<void(firebase::Status status)> /*callback*/) override {
    // Should never be called.
    FAIL();
  }

  void Watch(const std::string& key,
             const std::vector<std::string>& query_params,
             firebase::WatchClient* watch_client) override {
    watch_keys_.push_back(key);
    watch_queries_.push_back(query_params);
    watch_client_ = watch_client;
  }

  void UnWatch(firebase::WatchClient* /*watch_client*/) override {
    unwatch_count_++;
    watch_client_ = nullptr;
  }

  // CommitWatcher:
  void OnRemoteCommits(std::vector<Record> records) override {
    on_remote_commits_calls_++;
    for (auto& record : records) {
      commits_.push_back(std::move(record.commit));
      server_timestamps_.push_back(record.timestamp);
    }
  }

  void OnConnectionError() override { connection_error_calls_++; }

  void OnTokenExpired() override { token_expired_calls_++; }

  void OnMalformedNotification() override { malformed_notification_calls_++; }

 protected:
  const std::unique_ptr<PageCloudHandlerImpl> cloud_provider_;

  // These members keep track of calls made on the GCS client.
  std::vector<std::string> download_auth_tokens_;
  std::vector<std::string> download_keys_;
  std::vector<std::string> upload_auth_tokens_;
  std::vector<std::string> upload_keys_;
  std::vector<fsl::SizedVmo> upload_data_;

  // These members hold response data that GCS client is to return when called
  // by PageCloudHandlerImpl.
  uint64_t download_response_size_ = 0;
  zx::socket download_response_;
  gcs::Status download_status_ = gcs::Status::OK;

  // These members track calls made by PageCloudHandlerImpl to Firebase client.
  std::vector<std::string> get_keys_;
  std::vector<std::vector<std::string>> get_queries_;
  std::vector<std::string> put_keys_;
  std::vector<std::string> put_data_;
  std::vector<std::string> patch_keys_;
  std::vector<std::vector<std::string>> patch_queries_;
  std::vector<std::string> patch_data_;
  std::vector<std::string> watch_keys_;
  std::vector<std::vector<std::string>> watch_queries_;
  unsigned int unwatch_count_ = 0u;
  firebase::WatchClient* watch_client_ = nullptr;

  // These members hold response data that Firebase client is to return when
  // called by PageCloudHandlerImpl.
  std::unique_ptr<rapidjson::Document> get_response_;

  // These members track calls received from PageCloudHandlerImpl by this class
  // registered as a CommitWatcher.
  std::vector<Commit> commits_;
  std::vector<std::string> server_timestamps_;
  unsigned int on_remote_commits_calls_ = 0u;
  unsigned int connection_error_calls_ = 0u;
  unsigned int token_expired_calls_ = 0u;
  unsigned int malformed_notification_calls_ = 0u;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudHandlerImplTest);
};

TEST_F(PageCloudHandlerImplTest, AddCommit) {
  Commit commit("commit_id", "some_content");
  std::vector<Commit> commits;
  commits.push_back(std::move(commit));

  bool called;
  Status status;
  cloud_provider_->AddCommits(
      "this-is-a-token", std::move(commits),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_EQ(1u, patch_keys_.size());
  EXPECT_EQ("commits", patch_keys_[0]);
  ASSERT_EQ(1u, patch_queries_.size());
  EXPECT_EQ(std::vector<std::string>{"auth=this-is-a-token"},
            patch_queries_[0]);
  ASSERT_EQ(1u, patch_data_.size());
  EXPECT_EQ(
      "{\"commit_idV\":{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"timestamp\":{\".sv\":\"timestamp\"},"
      "\"batch_position\":0,"
      "\"batch_size\":1"
      "}}",
      patch_data_[0]);
  EXPECT_TRUE(watch_keys_.empty());
  EXPECT_EQ(0u, unwatch_count_);
}

TEST_F(PageCloudHandlerImplTest, AddMultipleCommits) {
  Commit commit1("id1", "content1");
  Commit commit2("id2", "content2");
  std::vector<Commit> commits;
  commits.push_back(std::move(commit1));
  commits.push_back(std::move(commit2));

  bool called;
  Status status;
  cloud_provider_->AddCommits(
      "", std::move(commits),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  ASSERT_EQ(1u, patch_keys_.size());
  ASSERT_EQ(patch_keys_.size(), patch_data_.size());
  EXPECT_EQ("commits", patch_keys_[0]);
  EXPECT_EQ(
      "{\"id1V\":{\"id\":\"id1V\",\"content\":\"content1V\","
      "\"timestamp\":{\".sv\":\"timestamp\"},"
      "\"batch_position\":0,\"batch_size\":2},"
      "\"id2V\":{\"id\":\"id2V\",\"content\":\"content2V\","
      "\"timestamp\":{\".sv\":\"timestamp\"},"
      "\"batch_position\":1,\"batch_size\":2}}",
      patch_data_[0]);
}

TEST_F(PageCloudHandlerImplTest, Watch) {
  cloud_provider_->WatchCommits("this-is-a-token", "", this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("commits", watch_keys_[0]);
  EXPECT_EQ(std::vector<std::string>{"auth=this-is-a-token"},
            watch_queries_[0]);
}

TEST_F(PageCloudHandlerImplTest, WatchUnwatch) {
  cloud_provider_->WatchCommits("", "", this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("commits", watch_keys_[0]);
  EXPECT_EQ(std::vector<std::string>{}, watch_queries_[0]);
  EXPECT_EQ(0u, unwatch_count_);

  cloud_provider_->UnwatchCommits(this);
  EXPECT_EQ(1u, unwatch_count_);
}

TEST_F(PageCloudHandlerImplTest, WatchWithQuery) {
  cloud_provider_->WatchCommits("", ServerTimestampToBytes(42), this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("commits", watch_keys_[0]);
  EXPECT_EQ((std::vector<std::string>{"orderBy=\"timestamp\"", "startAt=42"}),
            watch_queries_[0]);
}

// Tests handling a server event containing multiple separate (not batched)
// commits.
TEST_F(PageCloudHandlerImplTest, WatchAndGetMultipleCommits) {
  cloud_provider_->WatchCommits("", "", this);

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

  watch_client_->OnPatch("/", document);

  Commit expected_n1("id_1", "some_content");
  Commit expected_n2("id_2", "some_other_content");
  EXPECT_EQ(2u, commits_.size());
  EXPECT_EQ(2u, server_timestamps_.size());
  EXPECT_EQ(expected_n1, commits_[0]);
  EXPECT_EQ(ServerTimestampToBytes(42), server_timestamps_[0]);
  EXPECT_EQ(expected_n2, commits_[1]);
  EXPECT_EQ(ServerTimestampToBytes(43), server_timestamps_[1]);
  EXPECT_EQ(0u, malformed_notification_calls_);
}

// Tests handling a server event containing a complete batch of commits.
TEST_F(PageCloudHandlerImplTest, WatchAndGetCompleteBatch) {
  cloud_provider_->WatchCommits("", "", this);

  std::string put_content = R"({
    "id_1V": {
      "id": "id_1V",
      "content": "some_contentV",
      "timestamp": 43,
      "batch_position": 0,
      "batch_size": 2
    },
    "id_2V": {
      "id": "id_2V",
      "content": "some_other_contentV",
      "timestamp": 43,
      "batch_position": 1,
      "batch_size": 2
    }
  })";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  EXPECT_EQ(0u, on_remote_commits_calls_);
  watch_client_->OnPatch("/", document);

  Commit expected_n1("id_1", "some_content");
  Commit expected_n2("id_2", "some_other_content");
  EXPECT_EQ(1u, on_remote_commits_calls_);
  EXPECT_EQ(2u, commits_.size());
  EXPECT_EQ(2u, server_timestamps_.size());
  EXPECT_EQ(expected_n1, commits_[0]);
  EXPECT_EQ(ServerTimestampToBytes(43), server_timestamps_[0]);
  EXPECT_EQ(expected_n2, commits_[1]);
  EXPECT_EQ(ServerTimestampToBytes(43), server_timestamps_[1]);
  EXPECT_EQ(0u, malformed_notification_calls_);
}

// Tests handling a batch delivered over two separate calls.
TEST_F(PageCloudHandlerImplTest, WatchAndGetBatchInTwoChunks) {
  cloud_provider_->WatchCommits("", "", this);

  std::string content_1 = R"({
    "id_1V": {
      "id": "id_1V",
      "content": "some_contentV",
      "timestamp": 42,
      "batch_position": 0,
      "batch_size": 2
    }
  })";
  rapidjson::Document document_1;
  document_1.Parse(content_1.c_str(), content_1.size());
  ASSERT_FALSE(document_1.HasParseError());
  watch_client_->OnPatch("/", document_1);

  EXPECT_EQ(0u, on_remote_commits_calls_);
  EXPECT_EQ(0u, commits_.size());

  std::string content_2 = R"({
    "id_2V": {
      "id": "id_2V",
      "content": "some_other_contentV",
      "timestamp": 42,
      "batch_position": 1,
      "batch_size": 2
    }
  })";
  rapidjson::Document document_2;
  document_2.Parse(content_2.c_str(), content_2.size());
  ASSERT_FALSE(document_2.HasParseError());
  watch_client_->OnPatch("/", document_2);

  EXPECT_EQ(1u, on_remote_commits_calls_);
  Commit expected_n1("id_1", "some_content");
  Commit expected_n2("id_2", "some_other_content");
  ASSERT_EQ(2u, commits_.size());
  EXPECT_EQ(expected_n1, commits_[0]);
  EXPECT_EQ(expected_n2, commits_[1]);
  EXPECT_EQ(0u, malformed_notification_calls_);
}

// Tests handling a batch delivered over two separate calls in incorrect order.
TEST_F(PageCloudHandlerImplTest, WatchAndGetBatchInTwoChunksOutOfOrder) {
  cloud_provider_->WatchCommits("", "", this);

  std::string content_2 = R"({
    "id_2V": {
      "id": "id_2V",
      "content": "some_other_contentV",
      "timestamp": 42,
      "batch_position": 1,
      "batch_size": 2
    }
  })";
  rapidjson::Document document_2;
  document_2.Parse(content_2.c_str(), content_2.size());
  ASSERT_FALSE(document_2.HasParseError());
  watch_client_->OnPatch("/", document_2);

  EXPECT_EQ(0u, commits_.size());

  std::string content_1 = R"({
    "id_1V": {
      "id": "id_1V",
      "content": "some_contentV",
      "timestamp": 42,
      "batch_position": 0,
      "batch_size": 2
    }
  })";
  rapidjson::Document document_1;
  document_1.Parse(content_1.c_str(), content_1.size());
  ASSERT_FALSE(document_1.HasParseError());
  watch_client_->OnPatch("/", document_1);

  Commit expected_n1("id_1", "some_content");
  Commit expected_n2("id_2", "some_other_content");
  ASSERT_EQ(2u, commits_.size());
  EXPECT_EQ(expected_n1, commits_[0]);
  EXPECT_EQ(expected_n2, commits_[1]);
  EXPECT_EQ(0u, malformed_notification_calls_);
}

// Tests handling a server event containing a single commit.
TEST_F(PageCloudHandlerImplTest, WatchAndGetSingleCommit) {
  cloud_provider_->WatchCommits("", "", this);

  std::string put_content =
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"timestamp\":1472722368296"
      "}";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/commits/commit_idV", document);

  Commit expected_commit("commit_id", "some_content");
  std::string expected_timestamp = ServerTimestampToBytes(1472722368296);
  EXPECT_EQ(1u, commits_.size());
  EXPECT_EQ(expected_commit, commits_[0]);
  EXPECT_EQ(1u, server_timestamps_.size());
  EXPECT_EQ(expected_timestamp, server_timestamps_[0]);
}

// Verifies that the initial response when there is no matching commits is
// ignored.
TEST_F(PageCloudHandlerImplTest, WatchWhenThereIsNothingToWatch) {
  cloud_provider_->WatchCommits("", "", this);

  std::string put_content = "null";
  rapidjson::Document document;
  document.Parse(put_content.c_str(), put_content.size());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/", document);
  EXPECT_EQ(0u, malformed_notification_calls_);
  EXPECT_TRUE(commits_.empty());
}

// Verifies that malformed commit notifications are reported through
// OnMalformedNotification() callback and that processing further notifications
// is stopped.
TEST_F(PageCloudHandlerImplTest, WatchMalformedCommits) {
  rapidjson::Document document;
  EXPECT_EQ(0u, malformed_notification_calls_);
  EXPECT_EQ(0u, unwatch_count_);

  // Not a dictionary.
  document.Parse("[]");
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", "", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(1u, malformed_notification_calls_);
  EXPECT_EQ(1u, unwatch_count_);

  // Missing fields.
  document.Parse("{}");
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", "", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(2u, malformed_notification_calls_);
  EXPECT_EQ(2u, unwatch_count_);

  // Timestamp is not a number.
  const char content[] =
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"timestamp\":\"42\""
      "}";
  document.Parse(content);
  ASSERT_FALSE(document.HasParseError());
  cloud_provider_->WatchCommits("", "", this);
  watch_client_->OnPut("/commits/commit_idV", document);
  EXPECT_EQ(3u, malformed_notification_calls_);
}

// Verifies that connection errors are reported through the OnConnectionError()
// callback.
TEST_F(PageCloudHandlerImplTest, WatchConnectionError) {
  rapidjson::Document document;
  EXPECT_EQ(0u, connection_error_calls_);
  EXPECT_EQ(0u, token_expired_calls_);
  EXPECT_EQ(0u, unwatch_count_);

  cloud_provider_->WatchCommits("", "", this);
  watch_client_->OnConnectionError();
  EXPECT_EQ(1u, connection_error_calls_);
  EXPECT_EQ(0u, token_expired_calls_);
  EXPECT_EQ(1u, unwatch_count_);
}

// Verifies that auth revoked errors are reported to the client as token
// expired errors, so that they can retry setting the watcher.
TEST_F(PageCloudHandlerImplTest, WatchAuthRevoked) {
  EXPECT_EQ(0u, connection_error_calls_);
  EXPECT_EQ(0u, token_expired_calls_);
  EXPECT_EQ(0u, unwatch_count_);

  cloud_provider_->WatchCommits("", "", this);
  watch_client_->OnAuthRevoked("token no longer valid");

  EXPECT_EQ(0u, connection_error_calls_);
  EXPECT_EQ(1u, token_expired_calls_);
  EXPECT_EQ(1u, unwatch_count_);
}

TEST_F(PageCloudHandlerImplTest, GetCommits) {
  std::string get_response_content =
      "{\"id1V\":"
      "{\"content\":\"xyzV\","
      "\"id\":\"id1V\","
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

  bool called;
  Status status;
  std::vector<Record> records;
  cloud_provider_->GetCommits(
      "this-is-a-token", ServerTimestampToBytes(42),
      callback::Capture(callback::SetWhenCalled(&called), &status, &records));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  const Commit expected_commit_1("id1", "xyz");
  const Commit expected_commit_2("id2", "bazinga");
  EXPECT_EQ(2u, records.size());
  // Verify that commits are ordered by timestamp.
  EXPECT_EQ(expected_commit_2, records[0].commit);
  EXPECT_EQ(ServerTimestampToBytes(42), records[0].timestamp);
  EXPECT_EQ(expected_commit_1, records[1].commit);
  EXPECT_EQ(ServerTimestampToBytes(1472722368296), records[1].timestamp);

  EXPECT_EQ(1u, get_keys_.size());
  EXPECT_EQ(1u, get_queries_.size());
  EXPECT_EQ("commits", get_keys_[0]);
  EXPECT_EQ((std::vector<std::string>{"auth=this-is-a-token",
                                      "orderBy=\"timestamp\"", "startAt=42"}),
            get_queries_[0]);
}

// Verifies that out-of-order batch commits are reordered when retrieved through
// GetCommits().
TEST_F(PageCloudHandlerImplTest, GetCommitsBatch) {
  std::string get_response_content = R"({
    "id_1V": {
      "id": "id_1V",
      "content": "other_contentV",
      "timestamp": 43,
      "batch_position": 1,
      "batch_size": 2
    },
    "id_0V": {
      "id": "id_0V",
      "content": "some_contentV",
      "timestamp": 43,
      "batch_position": 0,
      "batch_size": 2
    }
  })";
  get_response_ = std::make_unique<rapidjson::Document>();
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  bool called;
  Status status;
  std::vector<Record> records;
  cloud_provider_->GetCommits(
      "", ServerTimestampToBytes(42),
      callback::Capture(callback::SetWhenCalled(&called), &status, &records));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  const Commit expected_commit_0("id_0", "some_content");
  const Commit expected_commit_1("id_1", "other_content");
  EXPECT_EQ(2u, records.size());
  // Verify that commits are ordered by the batch position.
  EXPECT_EQ(expected_commit_0, records[0].commit);
  EXPECT_EQ(ServerTimestampToBytes(43), records[0].timestamp);
  EXPECT_EQ(expected_commit_1, records[1].commit);
  EXPECT_EQ(ServerTimestampToBytes(43), records[1].timestamp);
}

TEST_F(PageCloudHandlerImplTest, GetCommitsWhenThereAreNone) {
  std::string get_response_content = "null";
  get_response_ = std::make_unique<rapidjson::Document>();
  get_response_->Parse(get_response_content.c_str(),
                       get_response_content.size());

  bool called;
  Status status;
  std::vector<Record> records;
  cloud_provider_->GetCommits(
      "", ServerTimestampToBytes(42),
      callback::Capture(callback::SetWhenCalled(&called), &status, &records));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(records.empty());
}

TEST_F(PageCloudHandlerImplTest, AddObject) {
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga", &data));

  bool called;
  Status status;
  cloud_provider_->AddObject(
      "this-is-a-token", "object_digest", std::move(data),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(upload_keys_.size(), upload_data_.size());
  EXPECT_EQ(std::vector<std::string>{"this-is-a-token"}, upload_auth_tokens_);
  EXPECT_EQ(std::vector<std::string>{"object_digestV"}, upload_keys_);

  std::string uploaded_content;
  ASSERT_TRUE(fsl::StringFromVmo(upload_data_[0], &uploaded_content));
  EXPECT_EQ("bazinga", uploaded_content);
}

TEST_F(PageCloudHandlerImplTest, GetObject) {
  std::string content = "bazinga";
  download_response_ = fsl::WriteStringToSocket(content);
  download_response_size_ = content.size();

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  cloud_provider_->GetObject("this-is-a-token", "object_digest",
                             callback::Capture(callback::SetWhenCalled(&called),
                                               &status, &size, &data));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  std::string data_str;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &data_str));
  EXPECT_EQ("bazinga", data_str);
  EXPECT_EQ(7u, data_str.size());
  EXPECT_EQ(7u, size);

  EXPECT_EQ(std::vector<std::string>{"this-is-a-token"}, download_auth_tokens_);
  EXPECT_EQ(std::vector<std::string>{"object_digestV"}, download_keys_);
}

TEST_F(PageCloudHandlerImplTest, GetObjectNotFound) {
  download_response_ = fsl::WriteStringToSocket("");
  download_status_ = gcs::Status::NOT_FOUND;

  bool called;
  Status status;
  uint64_t size;
  zx::socket data;
  cloud_provider_->GetObject("", "object_digest",
                             callback::Capture(callback::SetWhenCalled(&called),
                                               &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_EQ(0u, size);
}

}  // namespace
}  // namespace cloud_provider_firebase
