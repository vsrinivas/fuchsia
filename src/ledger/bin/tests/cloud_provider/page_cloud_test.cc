// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/commit_pack/commit_pack.h"
#include "src/ledger/bin/tests/cloud_provider/convert.h"
#include "src/ledger/bin/tests/cloud_provider/types.h"
#include "src/ledger/bin/tests/cloud_provider/validation_test.h"
#include "src/lib/uuid/uuid.h"

namespace cloud_provider {
namespace {

// Verifies that the given array of commits contains a commit of the given id
// and data.
::testing::AssertionResult CheckThatCommitsContain(
    const std::vector<CommitPackEntry>& entries, const std::string& id,
    const std::string& data) {
  for (auto& entry : entries) {
    if (entry.id != id) {
      continue;
    }

    if (entry.data != data) {
      return ::testing::AssertionFailure()
             << "The commit of the expected id: 0x" << ToHex(id) << " (" << id
             << ") "
             << " was found but its data doesn't match - expected: 0x"
             << ToHex(data) << " but found: " << ToHex(entry.data);
    }

    return ::testing::AssertionSuccess();
  }

  return ::testing::AssertionFailure()
         << "The commit of the id: " << ToHex(id) << " (" << id << ") "
         << " is missing.";
}

class PageCloudTest : public ValidationTest, public PageCloudWatcher {
 public:
  PageCloudTest() {}
  ~PageCloudTest() override {}

 protected:
  ::testing::AssertionResult GetPageCloud(fidl::VectorPtr<uint8_t> app_id,
                                          fidl::VectorPtr<uint8_t> page_id,
                                          PageCloudSyncPtr* page_cloud) {
    *page_cloud = PageCloudSyncPtr();
    Status status = Status::INTERNAL_ERROR;

    if (cloud_provider_->GetPageCloud(std::move(app_id), std::move(page_id),
                                      page_cloud->NewRequest(),
                                      &status) != ZX_OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the page cloud due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the page cloud, received status: "
             << fidl::ToUnderlying(status);
    }

    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult GetLatestPositionToken(
      PageCloudSyncPtr* page_cloud,
      std::unique_ptr<cloud_provider::PositionToken>* token) {
    Status status = Status::INTERNAL_ERROR;
    std::unique_ptr<cloud_provider::CommitPack> commit_pack;
    if ((*page_cloud)->GetCommits(nullptr, &status, &commit_pack, token) !=
        ZX_OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the position token due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the position token, received status: "
             << fidl::ToUnderlying(status);
    }

    return ::testing::AssertionSuccess();
  }

  int on_new_commits_calls_ = 0;
  std::vector<CommitPackEntry> on_new_commits_commits_;
  cloud_provider::PositionToken on_new_commits_position_token_;
  OnNewCommitsCallback on_new_commits_commits_callback_;

  cloud_provider::Status on_error_status_ = cloud_provider::Status::OK;

 private:
  // PageCloudWatcher:
  void OnNewCommits(CommitPack commits,
                    cloud_provider::PositionToken position_token,
                    OnNewCommitsCallback callback) override {
    std::vector<CommitPackEntry> entries;
    ASSERT_TRUE(DecodeCommitPack(commits, &entries));

    on_new_commits_calls_++;
    std::move(entries.begin(), entries.end(),
              std::back_inserter(on_new_commits_commits_));
    on_new_commits_position_token_ = std::move(position_token);
    on_new_commits_commits_callback_ = std::move(callback);
  }

  void OnNewObject(std::vector<uint8_t> /*id*/, fuchsia::mem::Buffer /*data*/,
                   OnNewObjectCallback /*callback*/) override {
    // We don't have any implementations yet that support this API.
    // TODO(ppi): add tests for the OnNewObject notifications.
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status status) override {
    on_error_status_ = status;
  }
};

TEST_F(PageCloudTest, GetPageCloud) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
}

TEST_F(PageCloudTest, GetNoCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  std::unique_ptr<cloud_provider::CommitPack> commit_pack;
  std::unique_ptr<PositionToken> token;
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK,
            page_cloud->GetCommits(nullptr, &status, &commit_pack, &token));
  EXPECT_EQ(Status::OK, status);
  ASSERT_TRUE(commit_pack);
  std::vector<CommitPackEntry> entries;
  ASSERT_TRUE(DecodeCommitPack(*commit_pack, &entries));
  EXPECT_TRUE(entries.empty());
  EXPECT_FALSE(token);
}

TEST_F(PageCloudTest, AddAndGetCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  std::unique_ptr<CommitPack> result;
  std::unique_ptr<PositionToken> token;
  ASSERT_EQ(ZX_OK, page_cloud->GetCommits(nullptr, &status, &result, &token));
  EXPECT_EQ(Status::OK, status);
  ASSERT_TRUE(result);
  ASSERT_TRUE(DecodeCommitPack(*result, &entries));
  EXPECT_EQ(2u, entries.size());
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id1", "data1"));
  EXPECT_TRUE(token);
}

TEST_F(PageCloudTest, GetCommitsByPositionToken) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Add one more commit.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  // Retrieve the commits again with the position token of `id1`.
  std::unique_ptr<CommitPack> result;
  ASSERT_EQ(ZX_OK,
            page_cloud->GetCommits(std::move(token), &status, &result, &token));
  EXPECT_EQ(Status::OK, status);
  ASSERT_TRUE(result);
  ASSERT_TRUE(DecodeCommitPack(*result, &entries));

  // As per the API contract, the response must include `id2` and everything
  // newer than it. It may or may not include `id0` and `id1`.
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id2", "data2"));
}

TEST_F(PageCloudTest, AddAndGetObjects) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));
  Status status = Status::INTERNAL_ERROR;
  // Generate a random ID - the current cloud provider implementations don't
  // erase storage objects upon .Erase(), and we want to avoid interference from
  // previous test runs.
  // TODO(ppi): use a fixed ID here once the cloud provider implementations
  // support erasing objects.
  const std::string id = uuid::Generate();
  ASSERT_EQ(ZX_OK, page_cloud->AddObject(
                       ToArray(id), std::move(data).ToTransport(), &status));
  EXPECT_EQ(Status::OK, status);

  ::fuchsia::mem::BufferPtr buffer_ptr;
  ASSERT_EQ(ZX_OK, page_cloud->GetObject(ToArray(id), &status, &buffer_ptr));
  EXPECT_EQ(Status::OK, status);
  std::string read_data;
  ASSERT_TRUE(fsl::StringFromVmo(*buffer_ptr, &read_data));
  EXPECT_EQ("bazinga!", read_data);
}

TEST_F(PageCloudTest, AddSameObjectTwice) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));
  Status status = Status::INTERNAL_ERROR;
  const std::string id = "some id";
  ASSERT_EQ(ZX_OK, page_cloud->AddObject(
                       ToArray(id), std::move(data).ToTransport(), &status));
  EXPECT_EQ(Status::OK, status);
  // Adding the same object again must succeed as per cloud provider contract.
  fsl::SizedVmo more_data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &more_data));
  ASSERT_EQ(ZX_OK,
            page_cloud->AddObject(ToArray(id),
                                  std::move(more_data).ToTransport(), &status));
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageCloudTest, WatchAndReceiveCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(ZX_OK,
            page_cloud->SetWatcher(nullptr, std::move(watcher), &status));
  EXPECT_EQ(Status::OK, status);

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  // The two commits could be delivered in one or two notifications. If they are
  // delivered over two notifications, the second one can only be delivered
  // after the client confirms having processed the first one by calling the
  // notification callback.
  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_EQ(ZX_OK, binding.WaitForMessage());
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

// Verifies that the pre-existing commits are also delivered when a watcher is
// registered.
TEST_F(PageCloudTest, WatchWithBacklog) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));
  Status status = Status::INTERNAL_ERROR;

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(ZX_OK,
            page_cloud->SetWatcher(nullptr, std::move(watcher), &status));
  EXPECT_EQ(Status::OK, status);

  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_EQ(ZX_OK, binding.WaitForMessage());
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

TEST_F(PageCloudTest, WatchWithPositionToken) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Set the watcher with the position token of `id1`.
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(ZX_OK, page_cloud->SetWatcher(std::move(token), std::move(watcher),
                                          &status));
  EXPECT_EQ(Status::OK, status);

  // Add one more commit.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id2", "data2")) {
    ASSERT_EQ(ZX_OK, binding.WaitForMessage());
    on_new_commits_commits_callback_();
  }

  // Add one more commit.
  more_entries = {{"id3", "data3"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id3", "data3")) {
    ASSERT_EQ(ZX_OK, binding.WaitForMessage());
    on_new_commits_commits_callback_();
  }
}

TEST_F(PageCloudTest, WatchWithPositionTokenBatch) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(ToArray("app_id"), ToArray("page_id"), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Set the watcher with the position token of `id1`.
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(ZX_OK, page_cloud->SetWatcher(std::move(token), std::move(watcher),
                                          &status));
  EXPECT_EQ(Status::OK, status);

  // Add two commits at once.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}, {"id3", "data3"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(ZX_OK, page_cloud->AddCommits(std::move(commit_pack), &status));
  EXPECT_EQ(Status::OK, status);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id2", "data2")) {
    ASSERT_EQ(ZX_OK, binding.WaitForMessage());
    on_new_commits_commits_callback_();
  }
  // The two commits must be delivered at the same time.
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id3", "data3"));
}

}  // namespace
}  // namespace cloud_provider
