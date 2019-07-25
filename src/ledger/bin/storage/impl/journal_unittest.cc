// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/set_when_called.h>

#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class JournalTest : public ledger::TestWithEnvironment {
 public:
  JournalTest()
      : encryption_service_(dispatcher()),
        page_storage_(&environment_, &encryption_service_,
                      std::make_unique<storage::fake::FakeDb>(dispatcher()), "page_id",
                      CommitPruningPolicy::NEVER),
        object_identifier_(page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(
            0u, 0u, MakeObjectDigest("value"))) {}

  ~JournalTest() override {}

  // Test:
  void SetUp() override {
    Status status;
    bool called;
    page_storage_.Init(callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);

    page_storage_.GetCommit(kFirstPageCommitId, callback::Capture(callback::SetWhenCalled(&called),
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
                                    callback::Capture(callback::SetWhenCalled(&called), &status));
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

  FXL_DISALLOW_COPY_AND_ASSIGN(JournalTest);
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

    std::vector<Entry> entries = GetCommitContents(*commit);
    ASSERT_THAT(entries, SizeIs(1));
    EXPECT_EQ(entries[0].key, "key");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);

    // Ledger's content is now a single entry "key" -> "value". Delete it.
    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, std::move(commit)));
    journal_->Delete("key");

    status = journal_->Commit(handler, &commit, &objects_to_sync);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit);

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
            0u, 0u, MakeObjectDigest("another value"));
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

    SetJournal(JournalImpl::Simple(&environment_, &page_storage_, first_commit_->Clone()));
    journal_->Put("1", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> commit_1;
    std::vector<ObjectIdentifier> objects_to_sync_1;
    status = journal_->Commit(handler, &commit_1, &objects_to_sync_1);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, commit_1);

    // Create a merge journal, adding only a key "2".
    SetJournal(JournalImpl::Merge(&environment_, &page_storage_, std::move(commit_0),
                                  std::move(commit_1)));
    journal_->Put("2", object_identifier_, KeyPriority::EAGER);

    std::unique_ptr<const Commit> merge_commit;
    std::vector<ObjectIdentifier> objects_to_sync_merge;
    status = journal_->Commit(handler, &merge_commit, &objects_to_sync_merge);
    ASSERT_EQ(status, Status::OK);
    ASSERT_NE(nullptr, merge_commit);

    // Expect the contents to have two keys: "0" and "2".
    std::vector<Entry> entries = GetCommitContents(*merge_commit);
    entries = GetCommitContents(*merge_commit);
    ASSERT_THAT(entries, SizeIs(2));
    EXPECT_EQ(entries[0].key, "0");
    EXPECT_EQ(entries[0].object_identifier, object_identifier_);
    EXPECT_EQ(entries[0].priority, KeyPriority::EAGER);

    EXPECT_EQ(entries[1].key, "2");
    EXPECT_EQ(entries[1].object_identifier, object_identifier_);
    EXPECT_EQ(entries[1].priority, KeyPriority::EAGER);
  }));
}

}  // namespace
}  // namespace storage
