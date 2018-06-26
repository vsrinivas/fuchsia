// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/page_cloud_impl.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/testing/test_page_cloud_handler.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

namespace cloud_provider_firebase {
namespace {

// Creates a dummy continuation token.
std::unique_ptr<cloud_provider::Token> MakeToken(
    convert::ExtendedStringView token_id) {
  auto token = std::make_unique<cloud_provider::Token>();
  token->opaque_id = convert::ToArray(token_id);
  return token;
}

class PageCloudImplTest : public gtest::TestLoopFixture,
                          cloud_provider::PageCloudWatcher {
 public:
  PageCloudImplTest() : firebase_auth_(dispatcher()), watcher_binding_(this) {
    auto handler = std::make_unique<TestPageCloudHandler>(dispatcher());
    handler_ = handler.get();
    page_cloud_impl_ = std::make_unique<PageCloudImpl>(
        &firebase_auth_, nullptr, nullptr, std::move(handler),
        page_cloud_.NewRequest());
  }

  // cloud_provider::PageCloudWatcher:
  void OnNewCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                    std::unique_ptr<cloud_provider::Token> position_token,
                    OnNewCommitsCallback callback) override {
    on_new_commits_calls_++;
    on_new_commits_commits_ = std::move(commits);
    on_new_commits_position_token_ = std::move(position_token);
    on_new_commits_commits_callback_ = callback;
  }

  void OnNewObject(fidl::VectorPtr<uint8_t> id, fuchsia::mem::Buffer data,
                   OnNewObjectCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status status) override {
    on_error_status_ = status;
  }

 protected:
  firebase_auth::TestFirebaseAuth firebase_auth_;
  cloud_provider::PageCloudPtr page_cloud_;
  TestPageCloudHandler* handler_ = nullptr;
  std::unique_ptr<PageCloudImpl> page_cloud_impl_;
  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding_;

  int on_new_commits_calls_ = 0;
  fidl::VectorPtr<cloud_provider::Commit> on_new_commits_commits_;
  std::unique_ptr<cloud_provider::Token> on_new_commits_position_token_;
  OnNewCommitsCallback on_new_commits_commits_callback_;

  cloud_provider::Status on_error_status_ = cloud_provider::Status::OK;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImplTest);
};

TEST_F(PageCloudImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  page_cloud_impl_->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  page_cloud_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageCloudImplTest, AddCommits) {
  fidl::VectorPtr<cloud_provider::Commit> commits;
  {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray("id_0");
    commit.data = convert::ToArray("data_0");
    commits.push_back(std::move(commit));
  }
  {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray("id_1");
    commit.data = convert::ToArray("data_1");
    commits.push_back(std::move(commit));
  }

  bool called;
  cloud_provider::Status status;
  page_cloud_->AddCommits(
      std::move(commits),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(2u, handler_->received_commits.size());
  EXPECT_EQ("id_0", handler_->received_commits[0].id);
  EXPECT_EQ("data_0", handler_->received_commits[0].content);
  EXPECT_EQ("id_1", handler_->received_commits[1].id);
  EXPECT_EQ("data_1", handler_->received_commits[1].content);
}

TEST_F(PageCloudImplTest, AddCommitsNetworkError) {
  fidl::VectorPtr<cloud_provider::Commit> commits;
  {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray("id_0");
    commit.data = convert::ToArray("data_0");
    commits.push_back(std::move(commit));
  }

  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  bool called;
  cloud_provider::Status status;
  page_cloud_->AddCommits(
      std::move(commits),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, GetCommits) {
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");

  bool called;
  cloud_provider::Status status;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> token;
  page_cloud_->GetCommits(MakeToken("5"),
                          callback::Capture(callback::SetWhenCalled(&called),
                                            &status, &commits, &token));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(2u, commits->size());
  EXPECT_EQ("id_0", convert::ToString(commits->at(0).id));
  EXPECT_EQ("data_0", convert::ToString(commits->at(0).data));
  EXPECT_EQ("id_1", convert::ToString(commits->at(1).id));
  EXPECT_EQ("data_1", convert::ToString(commits->at(1).data));
  EXPECT_EQ("43", convert::ToString(token->opaque_id));
}

TEST_F(PageCloudImplTest, GetCommitsEmpty) {
  bool called;
  cloud_provider::Status status;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> token;
  page_cloud_->GetCommits(MakeToken("5"),
                          callback::Capture(callback::SetWhenCalled(&called),
                                            &status, &commits, &token));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(0u, commits->size());
  EXPECT_FALSE(token);
}

TEST_F(PageCloudImplTest, GetCommitsNullToken) {
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");

  bool called;
  cloud_provider::Status status;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> token;
  page_cloud_->GetCommits(
      nullptr, callback::Capture(callback::SetWhenCalled(&called), &status,
                                 &commits, &token));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, commits->size());
  EXPECT_EQ("id_0", convert::ToString(commits->at(0).id));
  EXPECT_EQ("data_0", convert::ToString(commits->at(0).data));
  EXPECT_EQ("42", convert::ToString(token->opaque_id));
}

TEST_F(PageCloudImplTest, GetCommitsNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  bool called;
  cloud_provider::Status status;
  fidl::VectorPtr<cloud_provider::Commit> commits;
  std::unique_ptr<cloud_provider::Token> token;
  page_cloud_->GetCommits(MakeToken("5"),
                          callback::Capture(callback::SetWhenCalled(&called),
                                            &status, &commits, &token));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, AddObject) {
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));

  bool called;
  cloud_provider::Status status;
  page_cloud_->AddObject(
      convert::ToArray("abc"), std::move(data).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, handler_->added_objects.size());
  EXPECT_EQ("bazinga!", handler_->added_objects["abc"]);
}

TEST_F(PageCloudImplTest, AddObjectNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));

  bool called;
  cloud_provider::Status status;
  page_cloud_->AddObject(
      convert::ToArray("abc"), std::move(data).ToTransport(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, GetObject) {
  handler_->objects_to_return["abc"] = "bazinga!";

  bool called;
  cloud_provider::Status status;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(convert::ToArray("abc"),
                         callback::Capture(callback::SetWhenCalled(&called),
                                           &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(8u, size);
  std::string data_str;
  ASSERT_TRUE(fsl::BlockingCopyToString(std::move(data), &data_str));
  EXPECT_EQ("bazinga!", data_str);
}

TEST_F(PageCloudImplTest, GetObjectNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  handler_->objects_to_return["abc"] = "bazinga!";

  bool called;
  cloud_provider::Status status;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(convert::ToArray("abc"),
                         callback::Capture(callback::SetWhenCalled(&called),
                                           &status, &size, &data));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, SetWatcher) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      MakeToken("bazinga"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_TRUE(on_new_commits_commits_->empty());
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");

  handler_->DeliverRemoteCommits();
  RunLoopUntilIdle();
  EXPECT_EQ(2u, on_new_commits_commits_->size());
  EXPECT_EQ("id_0", convert::ToString(on_new_commits_commits_->at(0).id));
  EXPECT_EQ("data_0", convert::ToString(on_new_commits_commits_->at(0).data));
  EXPECT_EQ("id_1", convert::ToString(on_new_commits_commits_->at(1).id));
  EXPECT_EQ("data_1", convert::ToString(on_new_commits_commits_->at(1).data));
  EXPECT_EQ("43", convert::ToString(on_new_commits_position_token_->opaque_id));
  on_new_commits_commits_callback_();
}

TEST_F(PageCloudImplTest, SetWatcherNullToken) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      nullptr, std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, handler_->watch_call_min_timestamps.size());
  EXPECT_TRUE(handler_->watch_call_min_timestamps[0].empty());
}

TEST_F(PageCloudImplTest, SetWatcherNotificationsOneAtATime) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      MakeToken("bazinga"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_TRUE(on_new_commits_commits_->empty());
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->DeliverRemoteCommits();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_new_commits_commits_->size());
  EXPECT_EQ(1, on_new_commits_calls_);

  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");
  handler_->DeliverRemoteCommits();
  // Verify that the client does not receive another notification.
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_new_commits_commits_->size());
  EXPECT_EQ(1, on_new_commits_calls_);

  // Call the pending callback and verify that the client did receive the next
  // notification.
  on_new_commits_commits_callback_();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_new_commits_commits_->size());
  EXPECT_EQ(2, on_new_commits_calls_);
  on_new_commits_commits_callback_();
}

TEST_F(PageCloudImplTest, SetWatcherNetworkError) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      MakeToken("bazinga"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnConnectionError();
  RunLoopUntilIdle();
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, on_error_status_);
}

TEST_F(PageCloudImplTest, SetWatcherAuthError) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      MakeToken("bazinga"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnTokenExpired();
  RunLoopUntilIdle();
  EXPECT_EQ(cloud_provider::Status::AUTH_ERROR, on_error_status_);
}

TEST_F(PageCloudImplTest, SetWatcherParseError) {
  bool called;
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(
      MakeToken("bazinga"), std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnMalformedNotification();
  RunLoopUntilIdle();
  EXPECT_EQ(cloud_provider::Status::PARSE_ERROR, on_error_status_);
}

}  // namespace
}  // namespace cloud_provider_firebase
