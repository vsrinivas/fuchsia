// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/page_cloud_impl.h"

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/auth_provider/test/test_auth_provider.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/test/test_page_cloud_handler.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace cloud_provider_firebase {
namespace {

class PageCloudImplTest : public ::test::TestWithMessageLoop,
                          cloud_provider::PageCloudWatcher {
 public:
  PageCloudImplTest()
      : auth_provider_(message_loop_.task_runner()), watcher_binding_(this) {
    auto handler = std::make_unique<test::TestPageCloudHandler>(
        message_loop_.task_runner());
    handler_ = handler.get();
    page_cloud_impl_ = std::make_unique<PageCloudImpl>(
        &auth_provider_, nullptr, nullptr, std::move(handler),
        page_cloud_.NewRequest());
  }

  // cloud_provider::PageCloudWatcher:
  void OnNewCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                    fidl::Array<uint8_t> position_token,
                    const OnNewCommitsCallback& callback) override {
    on_new_commits_calls_++;
    on_new_commits_commits_ = std::move(commits);
    on_new_commits_position_token_ = std::move(position_token);
    on_new_commits_commits_callback_ = callback;
    message_loop_.PostQuitTask();
  }

  void OnNewObject(fidl::Array<uint8_t> id,
                   zx::vmo data,
                   const OnNewObjectCallback& callback) override {
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status status) override {
    on_error_status_ = status;
    message_loop_.PostQuitTask();
  }

 protected:
  auth_provider::test::TestAuthProvider auth_provider_;
  cloud_provider::PageCloudPtr page_cloud_;
  test::TestPageCloudHandler* handler_ = nullptr;
  std::unique_ptr<PageCloudImpl> page_cloud_impl_;
  fidl::Binding<cloud_provider::PageCloudWatcher> watcher_binding_;

  int on_new_commits_calls_ = 0;
  fidl::Array<cloud_provider::CommitPtr> on_new_commits_commits_;
  fidl::Array<uint8_t> on_new_commits_position_token_;
  OnNewCommitsCallback on_new_commits_commits_callback_;

  cloud_provider::Status on_error_status_ = cloud_provider::Status::OK;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImplTest);
};

TEST_F(PageCloudImplTest, EmptyWhenDisconnected) {
  bool on_empty_called = false;
  page_cloud_impl_->set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    message_loop_.PostQuitTask();
  });
  page_cloud_.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageCloudImplTest, AddCommits) {
  fidl::Array<cloud_provider::CommitPtr> commits;
  {
    auto commit = cloud_provider::Commit::New();
    commit->id = convert::ToArray("id_0");
    commit->data = convert::ToArray("data_0");
    commits.push_back(std::move(commit));
  }
  {
    auto commit = cloud_provider::Commit::New();
    commit->id = convert::ToArray("id_1");
    commit->data = convert::ToArray("data_1");
    commits.push_back(std::move(commit));
  }

  cloud_provider::Status status;
  page_cloud_->AddCommits(std::move(commits),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(2u, handler_->received_commits.size());
  EXPECT_EQ("id_0", handler_->received_commits[0].id);
  EXPECT_EQ("data_0", handler_->received_commits[0].content);
  EXPECT_EQ("id_1", handler_->received_commits[1].id);
  EXPECT_EQ("data_1", handler_->received_commits[1].content);
}

TEST_F(PageCloudImplTest, AddCommitsNetworkError) {
  fidl::Array<cloud_provider::CommitPtr> commits;
  {
    auto commit = cloud_provider::Commit::New();
    commit->id = convert::ToArray("id_0");
    commit->data = convert::ToArray("data_0");
    commits.push_back(std::move(commit));
  }

  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  cloud_provider::Status status;
  page_cloud_->AddCommits(std::move(commits),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, GetCommits) {
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");

  cloud_provider::Status status;
  fidl::Array<cloud_provider::CommitPtr> commits;
  fidl::Array<uint8_t> token;
  page_cloud_->GetCommits(
      convert::ToArray("5"),
      callback::Capture(MakeQuitTask(), &status, &commits, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(2u, commits.size());
  EXPECT_EQ("id_0", convert::ToString(commits[0]->id));
  EXPECT_EQ("data_0", convert::ToString(commits[0]->data));
  EXPECT_EQ("id_1", convert::ToString(commits[1]->id));
  EXPECT_EQ("data_1", convert::ToString(commits[1]->data));
  EXPECT_EQ("43", convert::ToString(token));
}

TEST_F(PageCloudImplTest, GetCommitsEmpty) {
  cloud_provider::Status status;
  fidl::Array<cloud_provider::CommitPtr> commits;
  fidl::Array<uint8_t> token;
  page_cloud_->GetCommits(
      convert::ToArray("5"),
      callback::Capture(MakeQuitTask(), &status, &commits, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_FALSE(commits.is_null());
  EXPECT_EQ(0u, commits.size());
  EXPECT_TRUE(token.is_null());
}

TEST_F(PageCloudImplTest, GetCommitsNullToken) {
  handler_->records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");

  cloud_provider::Status status;
  fidl::Array<cloud_provider::CommitPtr> commits;
  fidl::Array<uint8_t> token;
  page_cloud_->GetCommits(
      nullptr, callback::Capture(MakeQuitTask(), &status, &commits, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ("id_0", convert::ToString(commits[0]->id));
  EXPECT_EQ("data_0", convert::ToString(commits[0]->data));
  EXPECT_EQ("42", convert::ToString(token));
}

TEST_F(PageCloudImplTest, GetCommitsNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  cloud_provider::Status status;
  fidl::Array<cloud_provider::CommitPtr> commits;
  fidl::Array<uint8_t> token;
  page_cloud_->GetCommits(
      convert::ToArray("5"),
      callback::Capture(MakeQuitTask(), &status, &commits, &token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, AddObject) {
  zx::vmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));

  cloud_provider::Status status;
  page_cloud_->AddObject(convert::ToArray("abc"), std::move(data),
                         callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, handler_->added_objects.size());
  EXPECT_EQ("bazinga!", handler_->added_objects["abc"]);
}

TEST_F(PageCloudImplTest, AddObjectNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  zx::vmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));

  cloud_provider::Status status;
  page_cloud_->AddObject(convert::ToArray("abc"), std::move(data),
                         callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, GetObject) {
  handler_->objects_to_return["abc"] = "bazinga!";

  cloud_provider::Status status;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(
      convert::ToArray("abc"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(8u, size);
  std::string data_str;
  ASSERT_TRUE(fsl::BlockingCopyToString(std::move(data), &data_str));
  EXPECT_EQ("bazinga!", data_str);
}

TEST_F(PageCloudImplTest, GetObjectNetworkError) {
  handler_->status_to_return = cloud_provider_firebase::Status::NETWORK_ERROR;
  handler_->objects_to_return["abc"] = "bazinga!";

  cloud_provider::Status status;
  uint64_t size;
  zx::socket data;
  page_cloud_->GetObject(
      convert::ToArray("abc"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, status);
}

TEST_F(PageCloudImplTest, SetWatcher) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(convert::ToArray("bazinga"), std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_TRUE(on_new_commits_commits_.empty());
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");

  handler_->DeliverRemoteCommits();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, on_new_commits_commits_.size());
  EXPECT_EQ("id_0", convert::ToString(on_new_commits_commits_[0]->id));
  EXPECT_EQ("data_0", convert::ToString(on_new_commits_commits_[0]->data));
  EXPECT_EQ("id_1", convert::ToString(on_new_commits_commits_[1]->id));
  EXPECT_EQ("data_1", convert::ToString(on_new_commits_commits_[1]->data));
  EXPECT_EQ("43", convert::ToString(on_new_commits_position_token_));
  on_new_commits_commits_callback_();
}

TEST_F(PageCloudImplTest, SetWatcherNullToken) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(nullptr, std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);
  EXPECT_EQ(1u, handler_->watch_call_min_timestamps.size());
  EXPECT_TRUE(handler_->watch_call_min_timestamps[0].empty());
}

TEST_F(PageCloudImplTest, SetWatcherNotificationsOneAtATime) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(convert::ToArray("bazinga"), std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_TRUE(on_new_commits_commits_.empty());
  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_0", "data_0"), "42");
  handler_->DeliverRemoteCommits();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, on_new_commits_commits_.size());
  EXPECT_EQ(1, on_new_commits_calls_);

  handler_->notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id_1", "data_1"), "43");
  handler_->DeliverRemoteCommits();
  // Verify that the client does not receive another notification.
  message_loop_.task_runner()->PostDelayedTask(
      [this] { message_loop_.PostQuitTask(); },
      fxl::TimeDelta::FromMilliseconds(1));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, on_new_commits_commits_.size());
  EXPECT_EQ(1, on_new_commits_calls_);

  // Call the pending callback and verify that the client did receive the next
  // notification.
  on_new_commits_commits_callback_();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, on_new_commits_commits_.size());
  EXPECT_EQ(2, on_new_commits_calls_);
  on_new_commits_commits_callback_();
}

TEST_F(PageCloudImplTest, SetWatcherNetworkError) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(convert::ToArray("bazinga"), std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnConnectionError();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::NETWORK_ERROR, on_error_status_);
}

TEST_F(PageCloudImplTest, SetWatcherAuthError) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(convert::ToArray("bazinga"), std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnTokenExpired();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::AUTH_ERROR, on_error_status_);
}

TEST_F(PageCloudImplTest, SetWatcherParseError) {
  cloud_provider::Status status;
  cloud_provider::PageCloudWatcherPtr watcher;
  watcher_binding_.Bind(watcher.NewRequest());
  page_cloud_->SetWatcher(convert::ToArray("bazinga"), std::move(watcher),
                          callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::OK, status);

  EXPECT_EQ(cloud_provider::Status::OK, on_error_status_);
  page_cloud_impl_->OnMalformedNotification();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_provider::Status::PARSE_ERROR, on_error_status_);
}

}  // namespace
}  // namespace cloud_provider_firebase
