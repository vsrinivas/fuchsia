// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/merging/last_one_wins_merger.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {
std::function<void(storage::Journal*)> AddKeyValueToJournal(
    const std::string& key,
    const storage::ObjectId& object_id) {
  return [key, object_id](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK,
              journal->Put(key, object_id, storage::KeyPriority::EAGER));
  };
}

std::function<void(storage::Journal*)> DeleteKeyFromJournal(
    const std::string& key) {
  return [key](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK, journal->Delete(key));
  };
}

std::vector<storage::Entry> GetCommitContents(
    std::unique_ptr<storage::CommitContents> contents) {
  std::vector<storage::Entry> result;
  std::unique_ptr<storage::Iterator<const storage::Entry>> it =
      contents->begin();
  while (it->Valid()) {
    result.push_back(**it);
    it->Next();
  }
  return result;
}

class MergeResolverTest : public test::TestWithMessageLoop {
 public:
  MergeResolverTest() {}
  ~MergeResolverTest() override {}

 protected:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_storage_.reset(new storage::PageStorageImpl(
        message_loop_.task_runner(), message_loop_.task_runner(),
        tmp_dir_.path(), kRootPageId));
    EXPECT_EQ(storage::Status::OK, page_storage_->Init());
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal;
    EXPECT_EQ(
        storage::Status::OK,
        page_storage_->StartCommit(parent_id.ToString(),
                                   storage::JournalType::IMPLICIT, &journal));
    contents(journal.get());
    storage::Status actual_status;
    storage::CommitId actual_commit_id;
    journal->Commit(test::Capture([this] { message_loop_.PostQuitTask(); },
                                  &actual_status, &actual_commit_id));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, actual_status);
    return actual_commit_id;
  }

  std::unique_ptr<storage::PageStorageImpl> page_storage_;

 private:
  files::ScopedTempDir tmp_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MergeResolverTest);
};

TEST_F(MergeResolverTest, Empty) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "bar"));
  storage::CommitId commit_2 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMerger> strategy =
      std::make_unique<LastOneWinsMerger>(page_storage_.get());
  MergeResolver resolver(page_storage_.get(), std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.PostQuitTask(); });
  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
}

class VerifyingMerger : public MergeStrategy {
 public:
  VerifyingMerger(storage::CommitId head1,
                  storage::CommitId head2,
                  storage::CommitId ancestor)
      : head1_(head1), head2_(head2), ancestor_(ancestor) {}
  ~VerifyingMerger() override {}

  ftl::RefPtr<callback::Cancellable> Merge(
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) override {
    EXPECT_EQ(ancestor_, ancestor->GetId());
    storage::CommitId actual_head1_id = head_1->GetId();
    if (actual_head1_id != head1_ && actual_head1_id != head2_) {
      // Fail
      EXPECT_EQ(head1_, actual_head1_id);
    }
    storage::CommitId actual_head2_id = head_2->GetId();
    if (actual_head2_id != head1_ && actual_head2_id != head2_) {
      // Fail
      EXPECT_EQ(head2_, actual_head2_id);
    }
    return callback::CreateDoneCancellable();
  }

 private:
  const storage::CommitId head1_;
  const storage::CommitId head2_;
  const storage::CommitId ancestor_;
};

TEST_F(MergeResolverTest, CommonAncestor) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<VerifyingMerger> strategy =
      std::make_unique<VerifyingMerger>(commit_5, commit_3, commit_2);
  MergeResolver resolver(page_storage_.get(), std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.PostQuitTask(); });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
}

TEST_F(MergeResolverTest, LastOneWins) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<LastOneWinsMerger> strategy =
      std::make_unique<LastOneWinsMerger>(page_storage_.get());
  MergeResolver resolver(page_storage_.get(), std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.PostQuitTask(); });

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
  std::unique_ptr<const storage::Commit> commit;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetCommit(ids[0], &commit));
  std::vector<storage::Entry> content_vector =
      GetCommitContents(commit->GetContents());
  // Entries are ordered by keys
  EXPECT_EQ("key2", content_vector[0].key);
  EXPECT_EQ("val2.1", content_vector[0].object_id);
  EXPECT_EQ("key3", content_vector[1].key);
  EXPECT_EQ("val3.0", content_vector[1].object_id);
}

}  // namespace
}  // namespace ledger
