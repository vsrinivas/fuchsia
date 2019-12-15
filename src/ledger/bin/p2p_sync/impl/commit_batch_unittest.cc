// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/commit_batch.h"

#include <map>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/storage/testing/storage_matcher.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"

using storage::MatchesCommitIdAndBytes;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::Pair;
using testing::SizeIs;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

namespace p2p_sync {
namespace {

p2p_provider::P2PClientId MakeP2PClientId(uint8_t id) { return p2p_provider::P2PClientId({id}); }

std::vector<storage::PageStorage::CommitIdAndBytes> MakeCommits(
    std::vector<std::pair<std::string, std::string>> commits) {
  std::vector<storage::PageStorage::CommitIdAndBytes> result;
  for (auto& [id, bytes] : commits) {
    result.emplace_back(id, bytes);
  }
  return result;
}

// Returns a slice of a vector in a format matchable by gmock container matchers.
template <typename T>
std::tuple<const T*, size_t> Slice(const std::vector<T>& data, size_t start, size_t length) {
  return std::make_tuple(&data[start], length);
}

class FakePageStorage : public storage::PageStorageEmptyImpl {
 public:
  FakePageStorage() = default;
  ~FakePageStorage() override = default;

  void GetGenerationAndMissingParents(
      const storage::PageStorage::CommitIdAndBytes& ids_and_bytes,
      fit::function<void(ledger::Status, uint64_t, std::vector<storage::CommitId>)> callback)
      override {
    if (status_to_return_ != ledger::Status::OK) {
      callback(status_to_return_, 0, {});
      return;
    }
    const auto& [generation, missing_parents] = generation_and_missing_parents_[ids_and_bytes.id];
    callback(ledger::Status::OK, generation, missing_parents);
  }

  void AddCommitsFromSync(std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
                          const storage::ChangeSource /*source*/,
                          fit::function<void(ledger::Status)> callback) override {
    commits_from_sync_.push_back(std::move(ids_and_bytes));
    callback(status_to_return_);
  }

  ledger::Status status_to_return_ = ledger::Status::OK;
  std::map<storage::CommitId, std::pair<uint64_t, std::vector<storage::CommitId>>>
      generation_and_missing_parents_;
  std::vector<std::vector<storage::PageStorage::CommitIdAndBytes>> commits_from_sync_;
};

class FakeDelegate : public CommitBatch::Delegate {
 public:
  void RequestCommits(const p2p_provider::P2PClientId& device,
                      std::vector<storage::CommitId> ids) override {
    requested_commits_.emplace_back(device, std::move(ids));
  }

  std::vector<std::pair<p2p_provider::P2PClientId, std::vector<storage::CommitId>>>
      requested_commits_;
};

class CommitBatchTest : public ledger::TestLoopFixture {
 public:
  CommitBatchTest() : batch_(MakeP2PClientId(1u), &delegate_, &storage_) {
    batch_.SetOnDiscardable(ledger::SetWhenCalled(&on_discardable_called_));
  }
  ~CommitBatchTest() override = default;

 protected:
  FakePageStorage storage_;
  FakeDelegate delegate_;
  bool on_discardable_called_ = false;
  p2p_provider::P2PClientId device_ = MakeP2PClientId(1u);
  CommitBatch batch_;
};

TEST_F(CommitBatchTest, MarkAsReadyAndAddCommit) {
  batch_.MarkPeerReady();
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_FALSE(on_discardable_called_);

  batch_.AddToBatch(MakeCommits({{"id", "data"}}));
  EXPECT_THAT(storage_.commits_from_sync_,
              ElementsAre(ElementsAre(MatchesCommitIdAndBytes("id", "data"))));
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  EXPECT_TRUE(on_discardable_called_);
}

TEST_F(CommitBatchTest, AddCommitAndMarkAsReady) {
  batch_.AddToBatch(MakeCommits({{"id", "data"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_FALSE(on_discardable_called_);

  batch_.MarkPeerReady();
  EXPECT_THAT(storage_.commits_from_sync_,
              ElementsAre(ElementsAre(MatchesCommitIdAndBytes("id", "data"))));
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  EXPECT_TRUE(on_discardable_called_);
}

TEST_F(CommitBatchTest, RequestMissingParents) {
  batch_.MarkPeerReady();
  storage_.generation_and_missing_parents_["id"] = {15, {"parent1", "parent2"}};
  batch_.AddToBatch(MakeCommits({{"id", "data"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_,
              ElementsAre(Pair(device_, ElementsAre("parent1", "parent2"))));
  EXPECT_FALSE(on_discardable_called_);

  // Add the requested commits.
  batch_.AddToBatch(MakeCommits({{"parent1", "data1"}, {"parent2", "data2"}}));
  ASSERT_THAT(storage_.commits_from_sync_, SizeIs(1));
  EXPECT_TRUE(on_discardable_called_);

  auto& commits = storage_.commits_from_sync_[0];
  // We added all three commits.
  ASSERT_THAT(commits, UnorderedElementsAre(MatchesCommitIdAndBytes("parent1", "data1"),
                                            MatchesCommitIdAndBytes("parent2", "data2"),
                                            MatchesCommitIdAndBytes("id", "data")));
  // The commit "id" was added last, after its parents.
  EXPECT_EQ(commits[2].id, "id");
}

TEST_F(CommitBatchTest, RequestOnlyOnce) {
  batch_.MarkPeerReady();
  storage_.generation_and_missing_parents_["id1"] = {1, {"parent1", "parent2"}};
  storage_.generation_and_missing_parents_["id2"] = {1, {"parent1", "parent2"}};
  batch_.AddToBatch(MakeCommits({{"id1", "data1"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_,
              ElementsAre(Pair(device_, ElementsAre("parent1", "parent2"))));

  // Add a commit with the same parents. They are not requested again.
  delegate_.requested_commits_.clear();
  batch_.AddToBatch(MakeCommits({{"id2", "data2"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());

  // Add the parents one by one.
  batch_.AddToBatch(MakeCommits({{"parent1", "dataA"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  EXPECT_FALSE(on_discardable_called_);
  batch_.AddToBatch(MakeCommits({{"parent2", "dataB"}}));
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  ASSERT_THAT(storage_.commits_from_sync_, SizeIs(1));
  EXPECT_TRUE(on_discardable_called_);

  auto& commits = storage_.commits_from_sync_[0];
  // We added the two parents in any order, then the two child commits in any order.
  ASSERT_THAT(commits, SizeIs(4));

  EXPECT_THAT(Slice(commits, 0, 2),
              UnorderedElementsAre(MatchesCommitIdAndBytes("parent1", "dataA"),
                                   MatchesCommitIdAndBytes("parent2", "dataB")));
  EXPECT_THAT(Slice(commits, 2, 2), UnorderedElementsAre(MatchesCommitIdAndBytes("id1", "data1"),
                                                         MatchesCommitIdAndBytes("id2", "data2")));
}

// Tests that we can receive newer commits during a batch.
TEST_F(CommitBatchTest, ParentThenChild) {
  batch_.MarkPeerReady();
  storage_.generation_and_missing_parents_["id1"] = {1, {"id0"}};
  storage_.generation_and_missing_parents_["id2"] = {2, {"id1"}};
  batch_.AddToBatch(MakeCommits({{"id1", "data1"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, ElementsAre(Pair(device_, ElementsAre("id0"))));
  delegate_.requested_commits_.clear();

  batch_.AddToBatch(MakeCommits({{"id2", "data2"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());

  batch_.AddToBatch(MakeCommits({{"id0", "data0"}}));
  EXPECT_THAT(storage_.commits_from_sync_,
              ElementsAre(ElementsAre(MatchesCommitIdAndBytes("id0", "data0"),
                                      MatchesCommitIdAndBytes("id1", "data1"),
                                      MatchesCommitIdAndBytes("id2", "data2"))));
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  EXPECT_TRUE(on_discardable_called_);
}

// Tests that we don't behave unresonably in case of a cycle.
TEST_F(CommitBatchTest, CommitCycle) {
  batch_.MarkPeerReady();
  storage_.generation_and_missing_parents_["id1"] = {1, {"id0"}};
  storage_.generation_and_missing_parents_["id0"] = {1, {"id1"}};
  batch_.AddToBatch(MakeCommits({{"id1", "data1"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, ElementsAre(Pair(device_, ElementsAre("id0"))));
  delegate_.requested_commits_.clear();

  EXPECT_FALSE(on_discardable_called_);
  batch_.AddToBatch(MakeCommits({{"id0", "data0"}}));
  EXPECT_TRUE(on_discardable_called_);
}

// Check that the batch is aborted if we cannot list the parents.
TEST_F(CommitBatchTest, EmptyOnListMissingFailure) {
  EXPECT_FALSE(on_discardable_called_);
  storage_.status_to_return_ = ledger::Status::INTERNAL_ERROR;
  batch_.AddToBatch(MakeCommits({{"id1", "data1"}}));
  EXPECT_TRUE(on_discardable_called_);
}

// Check that the batch is aborted if we cannot add the commits.
TEST_F(CommitBatchTest, EmptyOnAddCommitsFailure) {
  batch_.AddToBatch(MakeCommits({{"id", "data"}}));
  EXPECT_THAT(storage_.commits_from_sync_, IsEmpty());
  EXPECT_THAT(delegate_.requested_commits_, IsEmpty());
  EXPECT_FALSE(on_discardable_called_);

  // ListMissingParents has already been called, but not AddCommitsFromSync.
  EXPECT_FALSE(on_discardable_called_);
  storage_.status_to_return_ = ledger::Status::INTERNAL_ERROR;
  batch_.MarkPeerReady();
  EXPECT_TRUE(on_discardable_called_);
}

}  // namespace
}  // namespace p2p_sync
