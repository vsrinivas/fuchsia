// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/page_cloud_impl.h"

#include <iterator>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <google/protobuf/util/time_util.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/bin/cloud_provider_firestore/app/testing/test_credentials_provider.h"
#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/encoding.h"
#include "peridot/bin/cloud_provider_firestore/firestore/testing/test_firestore_service.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {

void SetTimestamp(google::firestore::v1beta1::Document* document,
                  int64_t seconds, int32_t nanos) {
  google::protobuf::Timestamp& timestamp =
      *((*document->mutable_fields())["timestamp"].mutable_timestamp_value());
  timestamp.set_seconds(seconds);
  timestamp.set_nanos(nanos);
}

class TestPageCloudWatcher : public cloud_provider::PageCloudWatcher {
 public:
  TestPageCloudWatcher() {}
  ~TestPageCloudWatcher() override {}

  std::vector<cloud_provider::Commit> received_commits;
  std::vector<cloud_provider::Token> received_tokens;
  OnNewCommitsCallback pending_on_new_commit_callback = nullptr;

 private:
  // cloud_provider::PageCloudWatcher:
  void OnNewCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                    std::unique_ptr<cloud_provider::Token> position_token,
                    OnNewCommitsCallback callback) override {
    if (commits) {
      std::move(commits->begin(), commits->end(),
                std::back_inserter(received_commits));
    }
    if (position_token) {
      received_tokens.push_back(std::move(*position_token));
    }

    EXPECT_FALSE(pending_on_new_commit_callback);
    pending_on_new_commit_callback = std::move(callback);
  }

  void OnNewObject(fidl::VectorPtr<uint8_t> /*id*/,
                   fuchsia::mem::Buffer /*buffer*/,
                   OnNewObjectCallback /*callback*/) override {
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status /*status*/) override {
    FXL_NOTIMPLEMENTED();
  }
};

class PageCloudImplTest : public gtest::TestLoopFixture {
 public:
  PageCloudImplTest()
      : test_credentials_provider_(dispatcher()),
        page_cloud_impl_("page_path", &test_credentials_provider_,
                         &firestore_service_, page_cloud_.NewRequest()) {}
  ~PageCloudImplTest() override {}

 protected:
  cloud_provider::PageCloudPtr page_cloud_;
  TestCredentialsProvider test_credentials_provider_;
  TestFirestoreService firestore_service_;
  PageCloudImpl page_cloud_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImplTest);
};

TEST_F(PageCloudImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  page_cloud_impl_.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_cloud_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageCloudImplTest, AddCommits) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(cloud_provider::Commit{convert::ToArray("id0"),
                                           convert::ToArray("data0")});
  page_cloud_->AddCommits(
      std::move(commits),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.commit_records.size());
  const google::firestore::v1beta1::CommitRequest& request =
      firestore_service_.commit_records.front().request;
  EXPECT_EQ(2, request.writes_size());
  EXPECT_TRUE(request.writes(0).has_update());
  EXPECT_TRUE(request.writes(1).has_transform());
  EXPECT_EQ(request.writes(0).update().name(),
            request.writes(1).transform().document());

  auto response = google::firestore::v1beta1::CommitResponse();
  firestore_service_.commit_records.front().callback(grpc::Status::OK,
                                                     std::move(response));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(PageCloudImplTest, GetCommits) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> position_token;
  page_cloud_->GetCommits(
      nullptr, callback::Capture(callback::SetWhenCalled(&callback_called),
                                 &status, &commits, &position_token));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.run_query_records.size());

  std::vector<google::firestore::v1beta1::RunQueryResponse> responses;
  {
    // First batch contains one commit.
    fidl::VectorPtr<cloud_provider::Commit> batch_commits;
    batch_commits.push_back(cloud_provider::Commit{convert::ToArray("id0"),
                                                   convert::ToArray("data0")});
    google::firestore::v1beta1::RunQueryResponse response;
    ASSERT_TRUE(EncodeCommitBatch(batch_commits, response.mutable_document()));
    SetTimestamp(response.mutable_document(), 100, 1);
    responses.push_back(std::move(response));
  }
  {
    // The second batch contains two commits.
    fidl::VectorPtr<cloud_provider::Commit> batch_commits;
    batch_commits.push_back(cloud_provider::Commit{convert::ToArray("id1"),
                                                   convert::ToArray("data1")});
    batch_commits.push_back(cloud_provider::Commit{convert::ToArray("id2"),
                                                   convert::ToArray("data2")});
    google::firestore::v1beta1::RunQueryResponse response;
    ASSERT_TRUE(EncodeCommitBatch(batch_commits, response.mutable_document()));
    SetTimestamp(response.mutable_document(), 100, 2);
    responses.push_back(std::move(response));
  }

  firestore_service_.run_query_records.front().callback(grpc::Status::OK,
                                                        std::move(responses));
  RunLoopUntilIdle();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  ASSERT_TRUE(commits);
  // The result should be a flat vector of all three commits.
  EXPECT_EQ(3u, commits->size());

  EXPECT_TRUE(position_token);
  google::protobuf::Timestamp decoded_timestamp;
  ASSERT_TRUE(decoded_timestamp.ParseFromString(
      convert::ToString(position_token->opaque_id)));
  EXPECT_EQ(100, decoded_timestamp.seconds());
  EXPECT_EQ(2, decoded_timestamp.nanos());
}

TEST_F(PageCloudImplTest, GetCommitsQueryPositionToken) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  google::protobuf::Timestamp timestamp;
  timestamp.set_seconds(42);
  timestamp.set_nanos(1);
  std::string position_token_str;
  ASSERT_TRUE(timestamp.SerializeToString(&position_token_str));
  auto position_token = std::make_unique<cloud_provider::Token>();
  position_token->opaque_id = convert::ToArray(position_token_str);
  page_cloud_->GetCommits(
      std::move(position_token),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &commits, &position_token));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.run_query_records.size());

  const google::firestore::v1beta1::RunQueryRequest& request =
      firestore_service_.run_query_records.front().request;
  EXPECT_TRUE(request.has_structured_query());
  EXPECT_TRUE(request.structured_query().has_where());

  const google::firestore::v1beta1::StructuredQuery::Filter& filter =
      request.structured_query().where();
  EXPECT_TRUE(filter.has_field_filter());
  EXPECT_EQ("timestamp", filter.field_filter().field().field_path());
  EXPECT_EQ(google::firestore::v1beta1::
                StructuredQuery_FieldFilter_Operator_GREATER_THAN_OR_EQUAL,
            filter.field_filter().op());
  EXPECT_EQ(42, filter.field_filter().value().timestamp_value().seconds());
  EXPECT_EQ(1, filter.field_filter().value().timestamp_value().nanos());
}

TEST_F(PageCloudImplTest, AddObject) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("some_data", &data));
  page_cloud_->AddObject(
      convert::ToArray("some_id"), std::move(data).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.create_document_records.size());

  firestore_service_.create_document_records.front().callback(
      grpc::Status::OK, google::firestore::v1beta1::Document());

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(PageCloudImplTest, GetObject) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(
      convert::ToArray("some_id"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &size, &data));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());

  const std::string response_data = "some_data";
  google::firestore::v1beta1::Document response;
  *((*response.mutable_fields())["data"].mutable_bytes_value()) = response_data;
  firestore_service_.get_document_records.front().callback(grpc::Status(),
                                                           response);

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(response_data.size(), size);
  EXPECT_TRUE(data);

  std::string read_data;
  ASSERT_TRUE(fsl::BlockingCopyToString(std::move(data), &read_data));
  EXPECT_EQ("some_data", read_data);
}

TEST_F(PageCloudImplTest, GetObjectParseError) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(
      convert::ToArray("some_id"),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &size, &data));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1u, firestore_service_.get_document_records.size());

  // Create empty response with no data field.
  google::firestore::v1beta1::Document response;
  firestore_service_.get_document_records.front().callback(grpc::Status(),
                                                           response);

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::PARSE_ERROR, status);
}

TEST_F(PageCloudImplTest, SetWatcherResultOk) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  TestPageCloudWatcher watcher_impl;
  cloud_provider::PageCloudWatcherPtr watcher;
  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding(
      &watcher_impl, watcher.NewRequest());
  page_cloud_->SetWatcher(
      nullptr, std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, firestore_service_.listen_clients.size());
  EXPECT_FALSE(callback_called);

  auto response = google::firestore::v1beta1::ListenResponse();
  response.mutable_target_change()->set_target_change_type(
      google::firestore::v1beta1::TargetChange_TargetChangeType_CURRENT);
  firestore_service_.listen_clients[0]->OnResponse(std::move(response));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
}

TEST_F(PageCloudImplTest, SetWatcherGetCommits) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  TestPageCloudWatcher watcher_impl;
  cloud_provider::PageCloudWatcherPtr watcher;
  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding(
      &watcher_impl, watcher.NewRequest());
  page_cloud_->SetWatcher(
      nullptr, std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, firestore_service_.listen_clients.size());
  EXPECT_FALSE(callback_called);

  {
    auto response = google::firestore::v1beta1::ListenResponse();
    response.mutable_target_change()->set_target_change_type(
        google::firestore::v1beta1::TargetChange_TargetChangeType_CURRENT);
    firestore_service_.listen_clients[0]->OnResponse(std::move(response));
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(cloud_provider::Commit{convert::ToArray("id0"),
                                           convert::ToArray("data0")});
  google::protobuf::Timestamp protobuf_timestamp;
  ASSERT_TRUE(google::protobuf::util::TimeUtil::FromString(
      "2018-06-26T14:39:22+00:00", &protobuf_timestamp));
  std::string original_timestamp;
  ASSERT_TRUE(protobuf_timestamp.SerializeToString(&original_timestamp));

  auto response = google::firestore::v1beta1::ListenResponse();
  ASSERT_TRUE(EncodeCommitBatchWithTimestamp(
      commits, original_timestamp,
      response.mutable_document_change()->mutable_document()));
  firestore_service_.listen_clients[0]->OnResponse(std::move(response));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, watcher_impl.received_commits.size());
  EXPECT_EQ("id0", convert::ToString(watcher_impl.received_commits.front().id));
  EXPECT_EQ("data0",
            convert::ToString(watcher_impl.received_commits.front().data));
  EXPECT_EQ(1u, watcher_impl.received_tokens.size());
  EXPECT_EQ(original_timestamp,
            convert::ToString(watcher_impl.received_tokens.front().opaque_id));
  EXPECT_TRUE(watcher_impl.pending_on_new_commit_callback);
}

TEST_F(PageCloudImplTest, SetWatcherNotificationOneAtATime) {
  bool callback_called = false;
  auto status = cloud_provider::Status::INTERNAL_ERROR;
  TestPageCloudWatcher watcher_impl;
  cloud_provider::PageCloudWatcherPtr watcher;
  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding(
      &watcher_impl, watcher.NewRequest());
  page_cloud_->SetWatcher(
      nullptr, std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();

  {
    auto response = google::firestore::v1beta1::ListenResponse();
    response.mutable_target_change()->set_target_change_type(
        google::firestore::v1beta1::TargetChange_TargetChangeType_CURRENT);
    firestore_service_.listen_clients[0]->OnResponse(std::move(response));
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  // Deliver a commit notificiation from the cloud.
  {
    fidl::VectorPtr<cloud_provider::Commit> commits;
    commits.push_back(cloud_provider::Commit{convert::ToArray("id0"),
                                             convert::ToArray("data0")});
    google::protobuf::Timestamp protobuf_timestamp;
    ASSERT_TRUE(google::protobuf::util::TimeUtil::FromString(
        "2018-06-26T14:39:22+00:00", &protobuf_timestamp));
    std::string timestamp;
    ASSERT_TRUE(protobuf_timestamp.SerializeToString(&timestamp));

    auto response = google::firestore::v1beta1::ListenResponse();
    ASSERT_TRUE(EncodeCommitBatchWithTimestamp(
        commits, timestamp,
        response.mutable_document_change()->mutable_document()));
    firestore_service_.listen_clients[0]->OnResponse(std::move(response));
  }

  // Verify that the notification was delivered to the watcher.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, watcher_impl.received_commits.size());
  EXPECT_TRUE(watcher_impl.pending_on_new_commit_callback);

  // Deliver another commit notificiation from the cloud without calling the
  // watcher pending callback.
  {
    fidl::VectorPtr<cloud_provider::Commit> commits;
    commits.push_back(cloud_provider::Commit{convert::ToArray("id1"),
                                             convert::ToArray("data1")});
    google::protobuf::Timestamp protobuf_timestamp;
    ASSERT_TRUE(google::protobuf::util::TimeUtil::FromString(
        "2018-06-26T14:39:24+00:00", &protobuf_timestamp));
    std::string timestamp;
    ASSERT_TRUE(protobuf_timestamp.SerializeToString(&timestamp));

    auto response = google::firestore::v1beta1::ListenResponse();
    ASSERT_TRUE(EncodeCommitBatchWithTimestamp(
        commits, timestamp,
        response.mutable_document_change()->mutable_document()));
    firestore_service_.listen_clients[0]->OnResponse(std::move(response));
  }

  // Verify that another commit notification was not delivered.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, watcher_impl.received_commits.size());
  EXPECT_TRUE(watcher_impl.pending_on_new_commit_callback);

  // Call the pending callback and verify that the new commit notification was
  // delivered.
  watcher_impl.pending_on_new_commit_callback();
  watcher_impl.pending_on_new_commit_callback = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(2u, watcher_impl.received_commits.size());
  EXPECT_EQ("id0", convert::ToString(watcher_impl.received_commits[0].id));
  EXPECT_EQ("id1", convert::ToString(watcher_impl.received_commits[1].id));
  EXPECT_TRUE(watcher_impl.pending_on_new_commit_callback);
}

}  // namespace
}  // namespace cloud_provider_firestore
