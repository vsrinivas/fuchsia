// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/firebase/encoding.h"
#include "apps/ledger/firebase/firebase.h"
#include "apps/ledger/firebase/status.h"
#include "apps/ledger/glue/test/run_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

#include <rapidjson/document.h>

namespace cloud_provider {

class CloudProviderImplTest : public ::testing::Test,
                              public firebase::Firebase,
                              public NotificationWatcher {
 public:
  CloudProviderImplTest() : cloud_provider_(new CloudProviderImpl(this)) {}
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
      glue::test::QuitLoop();
    });
  }

  void Put(
      const std::string& key,
      const std::string& data,
      const std::function<void(firebase::Status status)>& callback) override {
    put_keys_.push_back(key);
    put_data_.push_back(data);
    message_loop_.task_runner()->PostTask([callback]() {
      callback(firebase::Status::OK);
      glue::test::QuitLoop();
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

  // NotificationWatcher:
  void OnNewNotification(const Notification& notification,
                         const std::string& timestamp) override {
    notifications_.push_back(notification);
    server_timestamps_.push_back(timestamp);
  }

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
  // registered as a NotificationWatcher.
  std::vector<Notification> notifications_;
  std::vector<std::string> server_timestamps_;

  mtl::MessageLoop message_loop_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProviderImplTest);
};

TEST_F(CloudProviderImplTest, AddNotification) {
  Notification notification(
      "commit_id", "some_content",
      std::map<StorageObjectId, Data>{{"object_a", "data_a"},
                                      {"object_b", "data_b"}});

  bool callback_called = false;
  cloud_provider_->AddNotification("app_id", "page_id", notification,
                                   [&callback_called](Status status) {
                                     EXPECT_EQ(Status::OK, status);
                                     callback_called = true;
                                   });
  glue::test::RunLoop();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1u, put_keys_.size());
  EXPECT_EQ(put_keys_.size(), put_data_.size());
  EXPECT_EQ("app_idV/page_idV/commit_idV", put_keys_[0]);
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
  cloud_provider_->WatchNotifications("app_id", "page_id", "", this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("app_idV/page_idV", watch_keys_[0]);
  EXPECT_EQ("", watch_queries_[0]);
  EXPECT_EQ(0u, unwatch_count_);

  cloud_provider_->UnwatchNotifications(this);
  EXPECT_EQ(1u, unwatch_count_);
}

TEST_F(CloudProviderImplTest, WatchWithQuery) {
  cloud_provider_->WatchNotifications("app_id", "page_id",
                                      ServerTimestampToBytes(42), this);
  EXPECT_EQ(1u, watch_keys_.size());
  EXPECT_EQ(1u, watch_queries_.size());
  EXPECT_EQ("app_idV/page_idV", watch_keys_[0]);
  EXPECT_EQ("orderBy=\"timestamp\"&startAt=42", watch_queries_[0]);
}

// Tests handling a server event containing multiple notifications.
TEST_F(CloudProviderImplTest, WatchAndGetNotifiedMultiple) {
  cloud_provider_->WatchNotifications("app_id", "page_id", "", this);

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
  document.Parse(put_content.c_str());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/", document);

  Notification expected_n1("id_1", "some_content",
                           std::map<StorageObjectId, Data>{});
  Notification expected_n2("id_2", "some_other_content",
                           std::map<StorageObjectId, Data>{});
  EXPECT_EQ(2u, notifications_.size());
  EXPECT_EQ(2u, server_timestamps_.size());
  EXPECT_EQ(expected_n1, notifications_[0]);
  EXPECT_EQ(ServerTimestampToBytes(42), server_timestamps_[0]);
  EXPECT_EQ(expected_n2, notifications_[1]);
  EXPECT_EQ(ServerTimestampToBytes(43), server_timestamps_[1]);
}

// Tests handling a server event containing a single notification.
TEST_F(CloudProviderImplTest, WatchAndGetNotifiedSingle) {
  cloud_provider_->WatchNotifications("app_id", "page_id", "", this);

  std::string put_content =
      "{\"id\":\"commit_idV\","
      "\"content\":\"some_contentV\","
      "\"objects\":{"
      "\"object_aV\":\"data_aV\","
      "\"object_bV\":\"data_bV\"},"
      "\"timestamp\":1472722368296"
      "}";
  rapidjson::Document document;
  document.Parse(put_content.c_str());
  ASSERT_FALSE(document.HasParseError());

  watch_client_->OnPut("/app_idV/page_idV/commit_idV", document);

  Notification expected_notification(
      "commit_id", "some_content",
      std::map<StorageObjectId, Data>{{"object_a", "data_a"},
                                      {"object_b", "data_b"}});
  std::string expected_timestamp = ServerTimestampToBytes(1472722368296);
  EXPECT_EQ(1u, notifications_.size());
  EXPECT_EQ(expected_notification, notifications_[0]);
  EXPECT_EQ(1u, server_timestamps_.size());
  EXPECT_EQ(expected_timestamp, server_timestamps_[0]);
}

TEST_F(CloudProviderImplTest, GetNotifications) {
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
  get_response_.reset(new rapidjson::Document());
  get_response_->Parse(get_response_content.c_str());

  bool callback_called = false;
  auto callback = [&callback_called](Status status,
                                     const std::vector<Record>& records) {
    EXPECT_EQ(Status::OK, status);
    callback_called = true;

    const Notification expected_notification_1(
        "id1", "xyz",
        std::map<StorageObjectId, Data>{{"object_a", "a"}, {"object_b", "b"}});
    const Notification expected_notification_2(
        "id2", "bazinga", std::map<StorageObjectId, Data>{});
    EXPECT_EQ(2u, records.size());
    EXPECT_EQ(expected_notification_1, records[0].notification);
    EXPECT_EQ(ServerTimestampToBytes(1472722368296), records[0].timestamp);
    EXPECT_EQ(expected_notification_2, records[1].notification);
    EXPECT_EQ(ServerTimestampToBytes(42), records[1].timestamp);
  };

  cloud_provider_->GetNotifications("app_id", "page_id",
                                    ServerTimestampToBytes(42), callback);
  glue::test::RunLoop();

  EXPECT_EQ(1u, get_keys_.size());
  EXPECT_EQ(1u, get_queries_.size());
  EXPECT_EQ("app_idV/page_idV", get_keys_[0]);
  EXPECT_EQ("orderBy=\"timestamp\"&startAt=42", get_queries_[0]);
  EXPECT_TRUE(callback_called);
}

}  // namespace cloud_provider
