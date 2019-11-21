// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/cloud_provider/types.h"
#include "src/ledger/bin/tests/cloud_provider/validation_test.h"
#include "src/ledger/lib/commit_pack/commit_pack.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/uuid/uuid.h"

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Property;

#define TABLE_FIELD_MATCHES(type, field, predicate)            \
  AllOf(Property("has_" #field, &type::has_##field, Eq(true)), \
        Property(#field, &type::field, predicate))

namespace cloud_provider {
namespace {

// Verifies that the given array of commits contains a commit of the given id
// and data.
::testing::AssertionResult CheckThatCommitsContain(const std::vector<CommitPackEntry>& entries,
                                                   const std::string& id, const std::string& data) {
  for (auto& entry : entries) {
    if (entry.id != id) {
      continue;
    }

    if (entry.data != data) {
      return ::testing::AssertionFailure()
             << "The commit of the expected id: 0x" << convert::ToHex(id) << " (" << id << ") "
             << " was found but its data doesn't match - expected: 0x" << convert::ToHex(data)
             << " but found: " << convert::ToHex(entry.data);
    }

    return ::testing::AssertionSuccess();
  }

  return ::testing::AssertionFailure()
         << "The commit of the id: " << convert::ToHex(id) << " (" << id << ") "
         << " is missing.";
}

class PageCloudTest : public ValidationTest, public PageCloudWatcher {
 public:
  PageCloudTest() = default;
  ~PageCloudTest() override = default;

 protected:
  ::testing::AssertionResult GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                                          PageCloudSyncPtr* page_cloud) {
    *page_cloud = PageCloudSyncPtr();
    Status status = Status::INTERNAL_ERROR;

    if (cloud_provider_->GetPageCloud(std::move(app_id), std::move(page_id),
                                      page_cloud->NewRequest(), &status) != ZX_OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the page cloud due to channel error.";
    }

    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "Failed to retrieve the page cloud, received status: "
                                           << fidl::ToUnderlying(status);
    }

    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult GetLatestPositionToken(
      PageCloudSyncPtr* page_cloud, std::unique_ptr<cloud_provider::PositionToken>* token) {
    Status status = Status::INTERNAL_ERROR;
    std::unique_ptr<cloud_provider::CommitPack> commit_pack;
    if ((*page_cloud)->GetCommits(nullptr, &status, &commit_pack, token) != ZX_OK) {
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

  ::testing::AssertionResult GetAndDecodeDiff(PageCloudSyncPtr* page_cloud,
                                              std::vector<uint8_t> commit_id,
                                              std::vector<std::vector<uint8_t>> possible_bases,
                                              Diff* diff) {
    Status status;
    std::unique_ptr<DiffPack> diff_pack;
    if ((*page_cloud)->GetDiff(commit_id, std::move(possible_bases), &status, &diff_pack) !=
        ZX_OK) {
      return ::testing::AssertionFailure() << "Failed to retrieve the diff due to a channel error.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "Failed to retrieve the diff, received status: " << fidl::ToUnderlying(status);
    }
    if (!diff_pack) {
      return ::testing::AssertionFailure() << "Received an empty diff pack.";
    }
    if (!DecodeFromBuffer(diff_pack->buffer, diff)) {
      return ::testing::AssertionFailure() << "Received invalid data in diff pack.";
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
  void OnNewCommits(CommitPack commits, cloud_provider::PositionToken position_token,
                    OnNewCommitsCallback callback) override {
    std::vector<CommitPackEntry> entries;
    ASSERT_TRUE(DecodeCommitPack(commits, &entries));

    on_new_commits_calls_++;
    std::move(entries.begin(), entries.end(), std::back_inserter(on_new_commits_commits_));
    on_new_commits_position_token_ = std::move(position_token);
    on_new_commits_commits_callback_ = std::move(callback);
  }

  void OnNewObject(std::vector<uint8_t> /*id*/, fuchsia::mem::Buffer /*data*/,
                   OnNewObjectCallback /*callback*/) override {
    // We don't have any implementations yet that support this API.
    // TODO(ppi): add tests for the OnNewObject notifications.
    FXL_NOTIMPLEMENTED();
  }

  void OnError(cloud_provider::Status status) override { on_error_status_ = status; }
};

TEST_F(PageCloudTest, GetPageCloud) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));
}

TEST_F(PageCloudTest, GetNoCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  std::unique_ptr<cloud_provider::CommitPack> commit_pack;
  std::unique_ptr<PositionToken> token;
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->GetCommits(nullptr, &status, &commit_pack, &token), ZX_OK);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(commit_pack);
  std::vector<CommitPackEntry> entries;
  ASSERT_TRUE(DecodeCommitPack(*commit_pack, &entries));
  EXPECT_TRUE(entries.empty());
  EXPECT_FALSE(token);
}

TEST_F(PageCloudTest, AddAndGetCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  std::unique_ptr<CommitPack> result;
  std::unique_ptr<PositionToken> token;
  ASSERT_EQ(page_cloud->GetCommits(nullptr, &status, &result, &token), ZX_OK);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(result);
  ASSERT_TRUE(DecodeCommitPack(*result, &entries));
  EXPECT_EQ(entries.size(), 2u);
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id1", "data1"));
  EXPECT_TRUE(token);
}

TEST_F(PageCloudTest, GetCommitsByPositionToken) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Add one more commit.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Retrieve the commits again with the position token of `id1`.
  std::unique_ptr<CommitPack> result;
  ASSERT_EQ(page_cloud->GetCommits(std::move(token), &status, &result, &token), ZX_OK);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(result);
  ASSERT_TRUE(DecodeCommitPack(*result, &entries));

  // As per the API contract, the response must include `id2` and everything
  // newer than it. It may or may not include `id0` and `id1`.
  EXPECT_TRUE(CheckThatCommitsContain(entries, "id2", "data2"));
}

TEST_F(PageCloudTest, AddAndGetObjects) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));
  Status status = Status::INTERNAL_ERROR;
  // Generate a random ID - the current cloud provider implementations don't
  // erase storage objects upon .Erase(), and we want to avoid interference from
  // previous test runs.
  // TODO(ppi): use a fixed ID here once the cloud provider implementations
  // support erasing objects.
  const std::string id = uuid::Generate();
  ASSERT_EQ(page_cloud->AddObject(convert::ToArray(id), std::move(data).ToTransport(), {}, &status),
            ZX_OK);
  EXPECT_EQ(status, Status::OK);

  ::fuchsia::mem::BufferPtr buffer_ptr;
  ASSERT_EQ(page_cloud->GetObject(convert::ToArray(id), &status, &buffer_ptr), ZX_OK);
  EXPECT_EQ(status, Status::OK);
  std::string read_data;
  ASSERT_TRUE(fsl::StringFromVmo(*buffer_ptr, &read_data));
  EXPECT_EQ(read_data, "bazinga!");
}

TEST_F(PageCloudTest, AddSameObjectTwice) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  fsl::SizedVmo data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &data));
  Status status = Status::INTERNAL_ERROR;
  const std::string id = "some id";
  ASSERT_EQ(page_cloud->AddObject(convert::ToArray(id), std::move(data).ToTransport(), {}, &status),
            ZX_OK);
  EXPECT_EQ(status, Status::OK);
  // Adding the same object again must succeed as per cloud provider contract.
  fsl::SizedVmo more_data;
  ASSERT_TRUE(fsl::VmoFromString("bazinga!", &more_data));
  ASSERT_EQ(
      page_cloud->AddObject(convert::ToArray(id), std::move(more_data).ToTransport(), {}, &status),
      ZX_OK);
  EXPECT_EQ(status, Status::OK);
}

TEST_F(PageCloudTest, WatchAndReceiveCommits) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));
  Status status = Status::INTERNAL_ERROR;
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(page_cloud->SetWatcher(nullptr, std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // The two commits could be delivered in one or two notifications. If they are
  // delivered over two notifications, the second one can only be delivered
  // after the client confirms having processed the first one by calling the
  // notification callback.
  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

// Verifies that the pre-existing commits are also delivered when a watcher is
// registered.
TEST_F(PageCloudTest, WatchWithBacklog) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));
  Status status = Status::INTERNAL_ERROR;

  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(page_cloud->SetWatcher(nullptr, std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  while (on_new_commits_commits_.size() < 2u) {
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    on_new_commits_commits_callback_();
  }
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id0", "data0"));
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id1", "data1"));
}

TEST_F(PageCloudTest, WatchWithPositionToken) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Set the watcher with the position token of `id1`.
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(page_cloud->SetWatcher(std::move(token), std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Add one more commit.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id2", "data2")) {
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    on_new_commits_commits_callback_();
  }

  // Add one more commit.
  more_entries = {{"id3", "data3"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id3", "data3")) {
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    on_new_commits_commits_callback_();
  }
}

TEST_F(PageCloudTest, WatchWithPositionTokenBatch) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add two commits.
  std::vector<CommitPackEntry> entries{{"id0", "data0"}, {"id1", "data1"}};
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(entries, &commit_pack));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Retrieve the position token of the newest of the two (`id1`).
  std::unique_ptr<cloud_provider::PositionToken> token;
  ASSERT_TRUE(GetLatestPositionToken(&page_cloud, &token));
  EXPECT_TRUE(token);

  // Set the watcher with the position token of `id1`.
  fidl::Binding<PageCloudWatcher> binding(this);
  PageCloudWatcherPtr watcher;
  binding.Bind(watcher.NewRequest());
  ASSERT_EQ(page_cloud->SetWatcher(std::move(token), std::move(watcher), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Add two commits at once.
  std::vector<CommitPackEntry> more_entries{{"id2", "data2"}, {"id3", "data3"}};
  ASSERT_TRUE(EncodeCommitPack(more_entries, &commit_pack));
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  while (!CheckThatCommitsContain(on_new_commits_commits_, "id2", "data2")) {
    ASSERT_EQ(binding.WaitForMessage(), ZX_OK);
    on_new_commits_commits_callback_();
  }
  // The two commits must be delivered at the same time.
  EXPECT_TRUE(CheckThatCommitsContain(on_new_commits_commits_, "id3", "data3"));
}

TEST_F(PageCloudTest, Diff_GetDiffFromEmpty) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add one commit, with a diff from the empty commit.
  Diff diff;
  diff.mutable_base_state()->set_empty_page({});
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::INSERTION)
                                                  .set_data(convert::ToArray("entryA_data"))));

  Commit commit;
  *commit.mutable_id() = convert::ToArray("id0");
  *commit.mutable_data() = convert::ToArray("data0");
  *commit.mutable_diff() = std::move(diff);

  std::vector<Commit> entries;
  entries.push_back(std::move(commit));

  CommitPack commit_pack;
  Commits commits{std::move(entries)};
  ASSERT_TRUE(EncodeToBuffer(&commits, &commit_pack.buffer));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // The cloud can only give a diff from the empty page.
  ASSERT_TRUE(GetAndDecodeDiff(&page_cloud, convert::ToArray("id0"), {}, &diff));
  ASSERT_TRUE(diff.has_base_state());
  EXPECT_TRUE(diff.base_state().is_empty_page());
  ASSERT_TRUE(diff.has_changes());
  ASSERT_EQ(diff.changes().size(), 1u);
  EXPECT_THAT(diff.changes()[0],
              AllOf(TABLE_FIELD_MATCHES(DiffEntry, entry_id, convert::ToArray("entryA")),
                    TABLE_FIELD_MATCHES(DiffEntry, operation, Operation::INSERTION),
                    TABLE_FIELD_MATCHES(DiffEntry, data, convert::ToArray("entryA_data"))));
}

TEST_F(PageCloudTest, Diff_GetMultipleDiff) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add one commit, with a diff from the empty commit.
  Diff diff;
  diff.mutable_base_state()->set_empty_page({});
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::INSERTION)
                                                  .set_data(convert::ToArray("entryA_data"))));
  Commit commit;
  *commit.mutable_id() = convert::ToArray("id0");
  *commit.mutable_data() = convert::ToArray("data0");
  *commit.mutable_diff() = std::move(diff);

  std::vector<Commit> entries;
  entries.push_back(std::move(commit));

  // Add a second commit deleting the entry inserted in the last commit.
  diff = Diff{};
  diff.mutable_base_state()->set_at_commit(convert::ToArray("id0"));
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::DELETION)
                                                  .set_data(convert::ToArray("entryA_data2"))));
  commit = {};
  *commit.mutable_id() = convert::ToArray("id1");
  *commit.mutable_data() = convert::ToArray("data1");
  *commit.mutable_diff() = std::move(diff);

  entries.push_back(std::move(commit));

  // Upload both commits.
  CommitPack commit_pack;
  Commits commits{std::move(entries)};
  ASSERT_TRUE(EncodeToBuffer(&commits, &commit_pack.buffer));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Read the second commit. The cloud gives a diff from the empty page.
  ASSERT_TRUE(GetAndDecodeDiff(&page_cloud, convert::ToArray("id1"), {}, &diff));
  // The cloud can only give a diff from the empty page.
  ASSERT_TRUE(diff.has_base_state());
  EXPECT_TRUE(diff.base_state().is_empty_page());
  // The diff is either empty, or the addition then the deletion.
  ASSERT_TRUE(diff.has_changes());

  auto entry_matcher = AllOf(TABLE_FIELD_MATCHES(DiffEntry, entry_id, convert::ToArray("entryA")),
                             TABLE_FIELD_MATCHES(DiffEntry, data,
                                                 AnyOf(convert::ToArray("entryA_data"),
                                                       convert::ToArray("entryA_data2"))));
  auto two_changes_matcher = ElementsAre(
      AllOf(entry_matcher, TABLE_FIELD_MATCHES(DiffEntry, operation, Operation::INSERTION)),
      AllOf(entry_matcher, TABLE_FIELD_MATCHES(DiffEntry, operation, Operation::DELETION)));
  EXPECT_THAT(diff, TABLE_FIELD_MATCHES(Diff, changes, AnyOf(IsEmpty(), two_changes_matcher)));
}

TEST_F(PageCloudTest, DiffCompat_GetNoDiff) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add one commit without diff.
  Commit commit;
  *commit.mutable_id() = convert::ToArray("id0");
  *commit.mutable_data() = convert::ToArray("data0");

  std::vector<Commit> entries;
  entries.push_back(std::move(commit));

  CommitPack commit_pack;
  Commits commits{std::move(entries)};
  ASSERT_TRUE(EncodeToBuffer(&commits, &commit_pack.buffer));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Request a diff for `id0'.
  Diff diff;
  ASSERT_TRUE(GetAndDecodeDiff(&page_cloud, convert::ToArray("id0"), {}, &diff));
  // The cloud can only give a diff from the commit itself.
  ASSERT_TRUE(diff.has_base_state());
  EXPECT_TRUE(diff.base_state().is_at_commit());
  EXPECT_EQ(diff.base_state().at_commit(), convert::ToArray("id0"));
  // The diff is empty.
  ASSERT_TRUE(diff.has_changes());
  EXPECT_EQ(diff.changes().size(), 0u);
}

TEST_F(PageCloudTest, DiffCompat_GetDiffFromNoDiff) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add one commit without diff.
  Commit commit;
  *commit.mutable_id() = convert::ToArray("id0");
  *commit.mutable_data() = convert::ToArray("data0");

  std::vector<Commit> entries;
  entries.push_back(std::move(commit));

  // Add one commit with diff based on this commit.
  Diff diff;
  diff.mutable_base_state()->set_at_commit(convert::ToArray("id0"));
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::DELETION)
                                                  .set_data(convert::ToArray("entryA_data"))));

  commit = {};
  *commit.mutable_id() = convert::ToArray("id1");
  *commit.mutable_data() = convert::ToArray("data1");
  *commit.mutable_diff() = std::move(diff);

  entries.push_back(std::move(commit));

  // Upload both commits.
  CommitPack commit_pack;
  Commits commits{std::move(entries)};
  ASSERT_TRUE(EncodeToBuffer(&commits, &commit_pack.buffer));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Ask for a diff for `id1' with an empty base list.
  ASSERT_TRUE(GetAndDecodeDiff(&page_cloud, convert::ToArray("id1"), {}, &diff));
  // The cloud can only give a diff from `id0'.
  ASSERT_TRUE(diff.has_base_state());
  EXPECT_TRUE(diff.base_state().is_at_commit());
  EXPECT_EQ(diff.base_state().at_commit(), convert::ToArray("id0"));
  // The diff must contain only the deletion.
  ASSERT_TRUE(diff.has_changes());
  ASSERT_EQ(diff.changes().size(), 1u);
  EXPECT_THAT(diff.changes()[0],
              AllOf(TABLE_FIELD_MATCHES(DiffEntry, entry_id, convert::ToArray("entryA")),
                    TABLE_FIELD_MATCHES(DiffEntry, operation, Operation::DELETION),
                    TABLE_FIELD_MATCHES(DiffEntry, data, convert::ToArray("entryA_data"))));
}

TEST_F(PageCloudTest, Diff_GetDiffIntermediateCommit) {
  PageCloudSyncPtr page_cloud;
  ASSERT_TRUE(GetPageCloud(convert::ToArray("app_id"), GetUniqueRandomId(), &page_cloud));

  // Add one commit, with a diff from the empty commit.
  Diff diff;
  diff.mutable_base_state()->set_empty_page({});
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::INSERTION)
                                                  .set_data(convert::ToArray("entryA_data"))));
  Commit commit;
  *commit.mutable_id() = convert::ToArray("id0");
  *commit.mutable_data() = convert::ToArray("data0");
  *commit.mutable_diff() = std::move(diff);

  std::vector<Commit> entries;
  entries.push_back(std::move(commit));

  // Add a second commit deleting the entry inserted in the last commit.
  diff = Diff{};
  diff.mutable_base_state()->set_at_commit(convert::ToArray("id0"));
  diff.mutable_changes()->push_back(std::move(DiffEntry()
                                                  .set_entry_id(convert::ToArray("entryA"))
                                                  .set_operation(Operation::DELETION)
                                                  .set_data(convert::ToArray("entryA_data2"))));

  commit = {};
  *commit.mutable_id() = convert::ToArray("id1");
  *commit.mutable_data() = convert::ToArray("data1");
  *commit.mutable_diff() = std::move(diff);

  entries.push_back(std::move(commit));

  // Upload both commits.
  CommitPack commit_pack;
  Commits commits{std::move(entries)};
  ASSERT_TRUE(EncodeToBuffer(&commits, &commit_pack.buffer));
  Status status = Status::INTERNAL_ERROR;
  ASSERT_EQ(page_cloud->AddCommits(std::move(commit_pack), &status), ZX_OK);
  EXPECT_EQ(status, Status::OK);

  // Read the first commit.
  ASSERT_TRUE(
      GetAndDecodeDiff(&page_cloud, convert::ToArray("id0"), {convert::ToArray("id1")}, &diff));
  // The cloud may either give a diff from the empty page or from `id1'.
  ASSERT_TRUE(diff.has_base_state());
  ASSERT_TRUE(diff.has_changes());
  ASSERT_EQ(diff.changes().size(), 1u);
  auto& change = diff.changes()[0];
  EXPECT_THAT(change, AllOf(TABLE_FIELD_MATCHES(DiffEntry, entry_id, convert::ToArray("entryA")),
                            TABLE_FIELD_MATCHES(DiffEntry, data,
                                                AnyOf(convert::ToArray("entryA_data"),
                                                      convert::ToArray("entryA_data2")))));
  ASSERT_TRUE(change.has_operation());
  EXPECT_TRUE((diff.base_state().is_empty_page() && change.operation() == Operation::INSERTION) ||
              (diff.base_state().at_commit() == convert::ToArray("id1") &&
               change.operation() == Operation::INSERTION));
}

}  // namespace
}  // namespace cloud_provider
