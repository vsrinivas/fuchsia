// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/random/uuid.h"
#include "peridot/public/lib/cloud_provider/validation/convert.h"
#include "peridot/public/lib/cloud_provider/validation/validation_test.h"

namespace cloud_provider {
namespace {

CommitPtr MakeCommit(const std::string& id, const std::string& data) {
  auto commit = cloud_provider::Commit::New();
  commit->id = ToArray(id);
  commit->data = ToArray(data);
  return commit;
}

// Verifies that the given array of commits contains a commit of the given id
// and data.
::testing::AssertionResult CheckThatCommitsContain(
    const fidl::Array<CommitPtr>& commits,
    const std::string& id,
    const std::string& data) {
  fidl::Array<uint8_t> id_array = ToArray(id);
  fidl::Array<uint8_t> data_array = ToArray(data);
  for (auto& commit : commits) {
    if (!commit->id.Equals(id_array)) {
      continue;
    }

    if (!commit->data.Equals(data_array)) {
      return ::testing::AssertionFailure()
             << "The commit of the expected id: 0x" << ToHex(id_array) << " ("
             << ToString(id_array) << ") "
             << " was found but its data doesn't match - expected: 0x"
             << ToHex(data_array) << " but found: " << ToHex(commit->data);
    }

    return ::testing::AssertionSuccess();
  }

  return ::testing::AssertionFailure()
         << "The commit of the id: " << ToHex(id_array) << " ("
         << ToString(id_array) << ") "
         << " is missing.";
}

class PageCloudTest : public ValidationTest, public PageCloudWatcher {
 public:
  PageCloudTest() {}
  ~PageCloudTest() override {}

 protected:
  ::testing::AssertionResult GetPageCloud(fidl::Array<uint8_t> app_id,
                                          fidl::Array<uint8_t> page_id,
                                          PageCloudPtr* page_cloud) {
    page_cloud->reset();
    Status status = Status::INTERNAL_ERROR;

    cloud_provider_->GetPageCloud(
        std::move(app_id), std::move(page_id), page_cloud->NewRequest(),
        [&status](Status got_status) { status = got_status; });

    if (!cloud_provider_.WaitForIncomingResponse()) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the page cloud due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the page cloud, received status: "
             << status;
    }

    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult GetLatestPositionToken(
      PageCloudPtr* page_cloud,
      fidl::Array<uint8_t>* token) {
    Status status = Status::INTERNAL_ERROR;
    (*page_cloud)
        ->GetCommits(nullptr,
                     [&status, &token](Status got_status,
                                       fidl::Array<CommitPtr> got_commits,
                                       fidl::Array<uint8_t> got_token) {
                       status = got_status;
                       *token = std::move(got_token);
                     });
    if (!page_cloud->WaitForIncomingResponse()) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the position token due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the position token, received status: "
             << status;
    }

    return ::testing::AssertionSuccess();
  }

  int on_new_commits_calls_ = 0;
  fidl::Array<cloud_provider::CommitPtr> on_new_commits_commits_;
  fidl::Array<uint8_t> on_new_commits_position_token_;
  OnNewCommitsCallback on_new_commits_commits_callback_;

  cloud_provider::Status on_error_status_ = cloud_provider::Status::OK;

 private:
  // PageCloudWatcher:
  void OnNewCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                    fidl::Array<uint8_t> position_token,
                    const OnNewCommitsCallback& callback) override {
    on_new_commits_calls_++;
    for (auto& commit : commits) {
      on_new_commits_commits_.push_back(std::move(commit));
    }
    on_new_commits_position_token_ = std::move(position_token);
    on_new_commits_commits_callback_ = callback;
  }

  void OnNewObject(fidl::Array<uint8_t> /*id*/,
                   fsl::SizedVmoTransportPtr /*data*/,
                   const OnNewObjectCallback& /*callback*/) override {
    // We don't have any implementations yet that support this API.
    // TODO(ppi): add tests for the OnNewObject notifications.
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status status) override {
    on_error_status_ = status;
  }
};

TEST_F(PageCloudTest, GetPageCloud) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
}

TEST_F(PageCloudTest, AddAndGetCommits) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  fidl::Array<CommitPtr> commits;
  commits.push_back(MakeCommit("id0", "data0"));
  commits.push_back(MakeCommit("id1", "data1"));
  Status status = Status::INTERNAL_ERROR;
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  commits.reset();
  page_cloud->GetCommits(
      nullptr,
      [&status, &commits](Status got_status, fidl::Array<CommitPtr> got_commits,
                          fidl::Array<uint8_t> got_token) {
        status = got_status;
        commits = std::move(got_commits);
      });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(2u, commits.size());
  EXPECT_TRUE(CheckThatCommitsContain(commits, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(commits, "id1", "data1"));
}

TEST_F(PageCloudTest, GetCommitsByPositionToken) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  // Add two commits.
  fidl::Array<CommitPtr> commits;
  commits.push_back(MakeCommit("id0", "data0"));
  commits.push_back(MakeCommit("id1", "data1"));
  Status status = Status::INTERNAL_ERROR;
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Retrieve the position token of the newest of the two (`id1`).
  fidl::Array<uint8_t> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Add one more commit.
  commits.reset();
  commits.push_back(MakeCommit("id2", "data2"));
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Retrieve the commits again with the position token of `id1`.
  commits.reset();
  page_cloud->GetCommits(
      std::move(token),
      [&status, &commits, &token](Status got_status,
                                  fidl::Array<CommitPtr> got_commits,
                                  fidl::Array<uint8_t> got_token) {
        status = got_status;
        commits = std::move(got_commits);
        token = std::move(got_token);
      });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // As per the API contract, the response must include `id1` and everything
  // newer than it. It may or may not include `id0`.
  EXPECT_TRUE(CheckThatCommitsContain(commits, "id1", "data1"));
  EXPECT_TRUE(CheckThatCommitsContain(commits, "id2", "data2"));
}

TEST_F(PageCloudTest, AddAndGetObjects) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));
  Status status = Status::INTERNAL_ERROR;
  // Generate a random ID - the current cloud provider implementations don't
  // erase storage objects upon .Erase(), and we want to avoid interference from
  // previous test runs.
  // TODO(ppi): use a fixed ID here once the cloud provider implementations
  // support erasing objects.
  const std::string id = fxl::GenerateUUID();
  page_cloud->AddObject(ToArray(id), std::move(data).ToTransport(),
                        [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  uint64_t size;
  zx::socket stream;
  page_cloud->GetObject(ToArray(id), [&status, &size, &stream](
                                         Status got_status, uint64_t got_size,
                                         zx::socket got_stream) {
    status = got_status;
    size = got_size;
    stream = std::move(got_stream);
  });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(8u, size);
  std::string data_str;
  ASSERT_TRUE(fsl::BlockingCopyToString(std::move(stream), &data_str));
  EXPECT_EQ("bazinga!", data_str);
}

TEST_F(PageCloudTest, WatchAndReceiveCommits) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  page_cloud->SetWatcher(nullptr, std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  fidl::Array<CommitPtr> commits;
  commits.push_back(MakeCommit("id0", "data0"));
  commits.push_back(MakeCommit("id1", "data1"));
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // The two commits could be delivered in one or two notifications. If they are
  // delivered over two notifications, the second one can only be delivered
  // after the client confirms having processed the first one by calling the
  // notification callback.
  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_TRUE(binding.WaitForIncomingMethodCall());
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

// Verifies that the pre-existing commits are also delivered when a watcher is
// registered.
TEST_F(PageCloudTest, WatchWithBacklog) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
  Status status = Status::INTERNAL_ERROR;

  fidl::Array<CommitPtr> commits;
  commits.push_back(MakeCommit("id0", "data0"));
  commits.push_back(MakeCommit("id1", "data1"));
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  page_cloud->SetWatcher(nullptr, std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_TRUE(binding.WaitForIncomingMethodCall());
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

TEST_F(PageCloudTest, WatchWithPositionToken) {
  PageCloudPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  // Add two commits.
  fidl::Array<CommitPtr> commits;
  commits.push_back(MakeCommit("id0", "data0"));
  commits.push_back(MakeCommit("id1", "data1"));
  Status status = Status::INTERNAL_ERROR;
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // Retrieve the position token of the newest of the two (`id1`).
  fidl::Array<uint8_t> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Set the watcher with the position token of `id1`.
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  page_cloud->SetWatcher(std::move(token), std::move(watcher),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  // As per the API contract, the commits delivered in notifications must
  // include `id1`. They may or may not include `id0`.
  while (!CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1")) {
    ASSERT_TRUE(binding.WaitForIncomingMethodCall());
    on_new_commits_commits_callback_();
  }

  // Add one more commit.
  commits.reset();
  commits.push_back(MakeCommit("id2", "data2"));
  page_cloud->AddCommits(std::move(commits),
                         [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(page_cloud.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id2", "data2")) {
    ASSERT_TRUE(binding.WaitForIncomingMethodCall());
    on_new_commits_commits_callback_();
  }
}

}  // namespace
}  // namespace cloud_provider
