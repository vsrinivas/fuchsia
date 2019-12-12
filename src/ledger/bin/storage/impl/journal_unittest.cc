// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/testing/storage_matcher.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace storage {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class JournalTest : public ledger::TestWithEnvironment {
 public:
  JournalTest()
      : encryption_service_(environment_.dispatcher()),
        page_storage_(&environment_, &encryption_service_,
                      std::make_unique<storage::fake::FakeDb>(dispatcher()), "page_id",
                      CommitPruningPolicy::NEVER),
        object_identifier_(page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(
            0u, MakeObjectDigest("value"))) {}
  JournalTest(const JournalTest&) = delete;
  JournalTest& operator=(const JournalTest&) = delete;
  ~JournalTest() override = default;

  // Test:
  void SetUp() override {
    Status status;
    bool called;
    clocks::DeviceIdManagerEmptyImpl device_id_manager;
    page_storage_.Init(&device_id_manager,
                       ledger::Capture(ledger::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);

    page_storage_.GetCommit(kFirstPageCommitId, ledger::Capture(ledger::SetWhenCalled(&called),
                                                                &status, &first_commit_));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
  }

  // Casts the given |Journal| to |JournalImpl| and updates |journal_| to have
  // this value.
  void SetJournal(std::unique_ptr<Journal> journal) {
    journal_ = std::unique_ptr<JournalImpl>(static_cast<JournalImpl*>(journal.release()));
  }

  std::vector<Entry> GetCommitContents(const Commit& commit) {
    bool called;
    Status status;
    std::vector<Entry> result;
    auto on_next = [&result](Entry e) {
      result.push_back(e);
      return true;
    };
    page_storage_.GetCommitContents(commit, "", std::move(on_next),
                                    ledger::Capture(ledger::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    return result;
  }

 protected:
  encryption::FakeEncryptionService encryption_service_;
  PageStorageImpl page_storage_;

  ObjectIdentifier object_identifier_;

  std::unique_ptr<JournalImpl> journal_;

  std::unique_ptr<const Commit> first_commit_;
};

TEST_F(JournalTest, CommitEmptyJournal) {
  SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    Status status = journal_->Commit(handler, &commit, &objects_to_sync);
    // Commiting an empty journal should result in a successful status, but a
    // null commit.
    ASSERT_EQ(status, Status::OK);
    ASSERT_EQ(commit, nullptr);
  }));
}

TEST_F(JournalTest, JournalsPutDeleteCommit) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("key", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    Status status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);
    ObjectDigest commit_root = commit->GetRootIdentifier().object_digest();
    objects_to_sync.clear();

    std::vector<Entry> entries = GetCommitContents(*commit);
    ASSERT_THAT(entries, SizeIs(1));
    EXPECT_EQ(entries[0].key, "key");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);
    EXPECT_FALSE(entries[0].entry_id.empty());

    // Ledger's content is now a single entry "key" -> "value". Delete it.
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, std::move(commit)));
    journal_->Delete("key");

    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    // Let the GC run.
    RunLoopUntilIdle();

    // Check that even after committing, we keep a live reference to the root of base commit.
    EXPECT_FALSE(page_storage_.GetObjectIdentifierFactory()->TrackDeletion(commit_root));

    ASSERT_THAT(GetCommitContents(*commit), ElementsAre());
  }));
}

TEST_F(JournalTest, JournalsPutRollback) {
  SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
  journal_->Put("key", object_identifier_, KeyPriority::EAGER);

  // The journal was not committed: the contents of page storage should not have
  // changed.
  journal_.reset();

  std::vector<std::unique_ptr<const Commit>> heads;
  Status status = page_storage_.GetHeadCommits(&heads);
  ASSERT_EQ(status, Status::OK);
  ASSERT_THAT(heads, SizeIs(1));
  EXPECT_EQ(heads[0]->GetId(), kFirstPageCommitId);
}

TEST_F(JournalTest, MultiplePutsDeletes) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    int size = 3;
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    Status status;
    // Insert keys {"0", "1", "2"}. Also insert key "0" a second time, with a
    // different value, and delete a non-existing key.
    for (int i = 0; i < size; i++) {
      journal_->Put(std::to_string(i), object_identifier_, KeyPriority::EAGER);
    }
    journal_->Delete("notfound");

    ObjectIdentifier object_identifier_2 =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(
            0u, MakeObjectDigest("another value"));
    journal_->Put("0", object_identifier_2, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    std::vector<Entry> entries = GetCommitContents(*commit);
    ASSERT_THAT(entries, SizeIs(size));
    for (int i = 0; i < size; i++) {
      EXPECT_EQ(entries[i].key, std::to_string(i));
      if (i == 0) {
        EXPECT_EQ(entries[i].object_identifier, object_identifier_2);
      } else {
        EXPECT_EQ(entries[i].object_identifier, object_identifier_);
      }
      EXPECT_EQ(entries[i].priority, KeyPriority::EAGER);
      EXPECT_FALSE(entries[i].entry_id.empty());
    }

    // Delete keys {"0", "2"}. Also insert a key, that is deleted on the same
    // journal.
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, std::move(commit)));
    journal_->Delete("0");
    journal_->Delete("2");
    journal_->Put("tmp", object_identifier_, KeyPriority::EAGER);
    journal_->Delete("tmp");

    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    // Check that there is only one entry left.
    entries = GetCommitContents(*commit);
    ASSERT_THAT(entries, SizeIs(1));
    EXPECT_EQ(entries[0].key, "1");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);
    EXPECT_FALSE(entries[0].entry_id.empty());
  }));
}

TEST_F(JournalTest, PutClear) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    int size = 3;
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    Status status;
    // Insert keys {"0", "1", "2"}.
    for (int i = 0; i < size; i++) {
      journal_->Put(std::to_string(i), object_identifier_, KeyPriority::EAGER);
    }

    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    ASSERT_THAT(GetCommitContents(*commit), SizeIs(size));

    // Clear the contents.
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, std::move(commit)));
    journal_->Clear();

    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    EXPECT_THAT(GetCommitContents(*commit), IsEmpty());
  }));
}

TEST_F(JournalTest, JournalsPutTwice) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("key", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    Status status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

    std::vector<Entry> entries = GetCommitContents(*commit);
    ASSERT_THAT(entries, SizeIs(1));
    EXPECT_EQ(entries[0].key, "key");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);
    EXPECT_FALSE(entries[0].entry_id.empty());

    // Ledger's content is now a single entry "key" -> "value". Try to insert it again.
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, commit->Clone()));
    journal_->Put("key", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit2;
    status = journal_->Commit(handler, &commit2, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_EQ(nullptr, commit2);
  }));
}

TEST_F(JournalTest, JournalsDeleteNonExisting) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Delete("key");

    std::unique_ptr<const Commit> commit;
    std::vector<ObjectIdentifier> objects_to_sync;
    Status status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_EQ(nullptr, commit);
  }));
}

TEST_F(JournalTest, MergeJournal) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // Create 2 commits from the |kFirstPageCommitId|, one with a key "0", and
    // one with a key "1".
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("0", object_identifier_, KeyPriority::EAGER);

    Status status;
    std::unique_ptr<const Commit> commit_0;
    std::vector<ObjectIdentifier> objects_to_sync_0;
    status = journal_->Commit(handler, &commit_0, &objects_to_sync_0);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_0);
    ObjectDigest commit0_root = commit_0->GetRootIdentifier().object_digest();
    objects_to_sync_0.clear();

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit_1;
    std::vector<ObjectIdentifier> objects_to_sync_1;
    status = journal_->Commit(handler, &commit_1, &objects_to_sync_1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_1);
    ObjectDigest commit1_root = commit_1->GetRootIdentifier().object_digest();
    objects_to_sync_1.clear();

    // Create a merge journal, adding only a key "2".
    SetJournal(JournalImpl::Merge(&environment_, &page_storage_, std::move(commit_0),
                                  std::move(commit_1)));
    journal_->Put("2", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit;
    std::vector<ObjectIdentifier> objects_to_sync_merge;
    status = journal_->Commit(handler, &merge_commit, &objects_to_sync_merge);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit);
    objects_to_sync_merge.clear();

    // Let the GC run.
    RunLoopUntilIdle();

    // Even after committing, we should have live references to the roots of commit 0 and 1.
    // Test this by trying to start a deletion.
    EXPECT_FALSE(page_storage_.GetObjectIdentifierFactory()->TrackDeletion(commit0_root));
    EXPECT_FALSE(page_storage_.GetObjectIdentifierFactory()->TrackDeletion(commit1_root));

    // Expect the contents to have two keys: "0" and "2".
    std::vector<Entry> entries = GetCommitContents(*merge_commit);
    entries = GetCommitContents(*merge_commit);
    ASSERT_THAT(entries, SizeIs(2));
    EXPECT_EQ(entries[0].key, "0");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);
    EXPECT_FALSE(entries[0].entry_id.empty());

    EXPECT_EQ(entries[1].key, "2");
    EXPECT_EQ(entries[1].object_identifier, object_identifier_);
    EXPECT_EQ(entries[1].priority, KeyPriority::EAGER);
    EXPECT_FALSE(entries[1].entry_id.empty());
  }));
}

TEST_F(JournalTest, MergesConsistent) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // Create 2 commits from the |kFirstPageCommitId|, one with a key "0", and
    // one with a key "1".
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("0", object_identifier_, KeyPriority::EAGER);

    Status status;
    std::unique_ptr<const Commit> commit_0;
    std::vector<ObjectIdentifier> objects_to_sync_0;
    status = journal_->Commit(handler, &commit_0, &objects_to_sync_0);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_0);

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit_1;
    std::vector<ObjectIdentifier> objects_to_sync_1;
    status = journal_->Commit(handler, &commit_1, &objects_to_sync_1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_1);

    // Create a merge journal, adding only a key "2".
    SetJournal(
        JournalImpl::Merge(&environment_, &page_storage_, commit_0->Clone(), commit_1->Clone()));
    journal_->Put("2", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit1;
    std::vector<ObjectIdentifier> objects_to_sync_merge1;
    status = journal_->Commit(handler, &merge_commit1, &objects_to_sync_merge1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit1);

    // Create a merge journal, adding only a key "2".
    SetJournal(
        JournalImpl::Merge(&environment_, &page_storage_, commit_0->Clone(), commit_1->Clone()));
    journal_->Put("2", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit2;
    std::vector<ObjectIdentifier> objects_to_sync_merge2;
    status = journal_->Commit(handler, &merge_commit2, &objects_to_sync_merge2);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit2);

    // The two merges should have the same id so they are treated like a single merge by Ledger.
    EXPECT_EQ(merge_commit1->GetId(), merge_commit2->GetId());
  }));
}

TEST_F(JournalTest, ChangesDifferentInsertion) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    Status status;
    std::unique_ptr<const Commit> commit1;
    std::vector<ObjectIdentifier> objects_to_sync;
    status = journal_->Commit(handler, &commit1, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit1);

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit2;
    status = journal_->Commit(handler, &commit2, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit2);

    EXPECT_NE(commit1->GetRootIdentifier(), commit2->GetRootIdentifier());
    EXPECT_NE(commit1->GetId(), commit2->GetId());

    std::vector<Entry> entries1 = GetCommitContents(*commit1);
    std::vector<Entry> entries2 = GetCommitContents(*commit2);

    EXPECT_EQ(WithoutEntryIds(entries1), WithoutEntryIds(entries2));
    EXPECT_NE(entries1, entries2);
  }));
}

TEST_F(JournalTest, ChangesDifferentDeletion) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    Status status;
    std::unique_ptr<const Commit> parent;
    std::vector<ObjectIdentifier> objects_to_sync;
    status = journal_->Commit(handler, &parent, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, parent);

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, parent->Clone()));
    journal_->Delete("1");

    std::unique_ptr<const Commit> commit1;
    status = journal_->Commit(handler, &commit1, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit1);

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, parent->Clone()));
    journal_->Delete("1");

    std::unique_ptr<const Commit> commit2;
    status = journal_->Commit(handler, &commit2, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit2);

    EXPECT_EQ(commit1->GetRootIdentifier(), commit2->GetRootIdentifier());
    EXPECT_NE(commit1->GetId(), commit2->GetId());

    std::vector<Entry> entries1 = GetCommitContents(*commit1);
    std::vector<Entry> entries2 = GetCommitContents(*commit2);

    EXPECT_EQ(entries1, entries2);
  }));
}

using MergeTestEntry = std::tuple<std::string, std::tuple<uint32_t, std::string>, KeyPriority>;
using MergeTestParam = std::pair<MergeTestEntry, MergeTestEntry>;

class JournalMergeTest : public JournalTest, public ::testing::WithParamInterface<MergeTestParam> {
 public:
  ObjectIdentifier MakeObjectIdentifier(std::tuple<uint32_t, std::string> components) {
    auto& [key_index, object_digest] = components;
    return page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(
        key_index, MakeObjectDigest(object_digest));
  }

  void Put(MergeTestEntry entry) {
    auto& [key, object_identifier_components, priority] = entry;
    journal_->Put(key, MakeObjectIdentifier(object_identifier_components), priority);
  }

  testing::Matcher<const Entry&> Matches(const MergeTestEntry& entry) {
    auto& [key, object_identifier_components, priority] = entry;
    return MatchesEntry({key, MakeObjectIdentifier(object_identifier_components), priority});
  }
};

TEST_P(JournalMergeTest, MergeEntryIdDifferent) {
  ASSERT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // This test relies on the parameter entries sorting between "0" and "3", and the two being
    // different.
    auto [entry_params1, entry_params2] = GetParam();

    // Create 2 commits from the |kFirstPageCommitId|, one with a key "0", and
    // one with a key "1".
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("0", object_identifier_, KeyPriority::EAGER);

    Status status;
    std::unique_ptr<const Commit> commit_0;
    std::vector<ObjectIdentifier> objects_to_sync_0;
    status = journal_->Commit(handler, &commit_0, &objects_to_sync_0);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_0);

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit_1;
    std::vector<ObjectIdentifier> objects_to_sync_1;
    status = journal_->Commit(handler, &commit_1, &objects_to_sync_1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_1);

    // Create a merge journal, inserting key "3" and the first parameter.
    SetJournal(
        JournalImpl::Merge(&environment_, &page_storage_, commit_0->Clone(), commit_1->Clone()));
    Put(entry_params1);
    journal_->Put("3", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit1;
    std::vector<ObjectIdentifier> objects_to_sync_merge1;
    status = journal_->Commit(handler, &merge_commit1, &objects_to_sync_merge1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit1);

    // Create a merge journal, inserting key "3" and the second parameter.
    SetJournal(
        JournalImpl::Merge(&environment_, &page_storage_, commit_0->Clone(), commit_1->Clone()));
    Put(entry_params2);
    journal_->Put("3", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit2;
    std::vector<ObjectIdentifier> objects_to_sync_merge2;
    status = journal_->Commit(handler, &merge_commit2, &objects_to_sync_merge2);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit2);

    // Get the entries for both commits.
    std::vector<Entry> entries1 = GetCommitContents(*merge_commit1);
    std::vector<Entry> entries2 = GetCommitContents(*merge_commit2);

    ASSERT_THAT(entries1, SizeIs(3));
    ASSERT_THAT(entries2, SizeIs(3));
    // The entries that were already present are unmodified.
    // Test key "0"
    EXPECT_EQ(entries1[0].key, "0");
    EXPECT_EQ(entries1[0], entries2[0]);

    // Test the parameter entries.
    EXPECT_THAT(entries1[1], Matches(entry_params1));
    EXPECT_THAT(entries2[1], Matches(entry_params2));
    EXPECT_NE(entries1[1].entry_id, entries2[1].entry_id);

    // Entry "3" has different entry ids.
    EXPECT_EQ(entries1[2].key, "3");
    EXPECT_EQ(WithoutEntryId(entries1[2]), WithoutEntryId(entries2[2]));
    EXPECT_NE(entries1[2].entry_id, entries2[2].entry_id);
  }));
}

INSTANTIATE_TEST_SUITE_P(
    JournalMergeTest, JournalMergeTest,
    ::testing::Values(std::make_pair(MergeTestEntry("2", {0, "digest"}, KeyPriority::EAGER),
                                     MergeTestEntry("21", {0, "digest"}, KeyPriority::EAGER)),
                      std::make_pair(MergeTestEntry("2", {0, "digest"}, KeyPriority::EAGER),
                                     MergeTestEntry("2", {1, "digest"}, KeyPriority::EAGER)),
                      std::make_pair(MergeTestEntry("2", {0, "digest"}, KeyPriority::EAGER),
                                     MergeTestEntry("2", {0, "digest2"}, KeyPriority::EAGER)),
                      std::make_pair(MergeTestEntry("2", {0, "digest"}, KeyPriority::EAGER),
                                     MergeTestEntry("2", {0, "digest"}, KeyPriority::LAZY))));

}  // namespace
}  // namespace storage
