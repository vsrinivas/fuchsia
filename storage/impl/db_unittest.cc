// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/db.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/fake/fake_page_storage.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/impl/journal_db_impl.h"
#include "apps/ledger/storage/impl/store/object_store.h"
#include "apps/ledger/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"

namespace storage {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

EntryChange NewEntryChange(std::string key,
                           std::string object_id,
                           KeyPriority priority) {
  EntryChange change;
  change.deleted = false;
  change.entry.key.swap(key);
  change.entry.object_id.swap(object_id);
  change.entry.priority = priority;
  return change;
}

EntryChange NewRemoveEntryChange(std::string key) {
  EntryChange change;
  change.deleted = true;
  change.entry.key.swap(key);
  return change;
}

void ExpectChangesEqual(const EntryChange& expected, const EntryChange& found) {
  EXPECT_EQ(expected.deleted, found.deleted);
  EXPECT_EQ(expected.entry.key, found.entry.key);
  if (!expected.deleted) {
    // If the entry is deleted, object_id and priority are not valid.
    EXPECT_EQ(expected.entry.object_id, found.entry.object_id);
    EXPECT_EQ(expected.entry.priority, found.entry.priority);
  }
}

class DBTest : public ::testing::Test {
 public:
  DBTest()
      : page_storage_(ObjectId(kObjectIdSize, 'a')),
        object_store_(&page_storage_) {}

  ~DBTest() override {}

  // Test:
  void SetUp() override { std::srand(0); }

 protected:
  std::unique_ptr<DB> GetDB() {
    std::unique_ptr<DB> db = std::unique_ptr<DB>(new DB(tmp_dir_.path()));
    EXPECT_EQ(Status::OK, db->Init());
    return db;
  }

  fake::FakePageStorage page_storage_;
  ObjectStore object_store_;

 private:
  files::ScopedTempDir tmp_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DBTest);
};

TEST_F(DBTest, HeadCommits) {
  std::unique_ptr<DB> db = GetDB();

  std::vector<CommitId> heads;
  EXPECT_EQ(Status::OK, db->GetHeads(&heads));
  EXPECT_TRUE(heads.empty());

  CommitId cid = RandomId(kCommitIdSize);
  EXPECT_EQ(Status::OK, db->AddHead(cid));
  EXPECT_EQ(Status::OK, db->GetHeads(&heads));
  EXPECT_EQ(1u, heads.size());
  EXPECT_EQ(cid, heads[0]);

  EXPECT_EQ(Status::OK, db->RemoveHead(cid));
  EXPECT_EQ(Status::OK, db->GetHeads(&heads));
  EXPECT_TRUE(heads.empty());
}

TEST_F(DBTest, Commits) {
  std::unique_ptr<DB> db = GetDB();

  std::vector<CommitId> parents;
  parents.push_back(RandomId(kCommitIdSize));

  std::unique_ptr<Commit> stored_commit;
  std::string storage_bytes;
  CommitImpl commit(&object_store_, RandomId(kCommitIdSize), 123,
                    RandomId(kObjectIdSize), parents);

  EXPECT_EQ(Status::NOT_FOUND,
            db->GetCommitStorageBytes(commit.GetId(), &storage_bytes));

  EXPECT_EQ(Status::OK, db->AddCommitStorageBytes(commit.GetId(),
                                                  commit.GetStorageBytes()));
  EXPECT_EQ(Status::OK,
            db->GetCommitStorageBytes(commit.GetId(), &storage_bytes));
  EXPECT_EQ(storage_bytes, commit.GetStorageBytes());

  EXPECT_EQ(Status::OK, db->RemoveCommit(commit.GetId()));
  EXPECT_EQ(Status::NOT_FOUND,
            db->GetCommitStorageBytes(commit.GetId(), &storage_bytes));
}

TEST_F(DBTest, Journals) {
  std::unique_ptr<DB> db = GetDB();
  CommitId commit_id = RandomId(kCommitIdSize);

  std::unique_ptr<Journal> implicit_journal;
  std::unique_ptr<Journal> explicit_journal;
  EXPECT_EQ(Status::OK, db->CreateJournal(JournalType::IMPLICIT, commit_id,
                                          &implicit_journal));
  EXPECT_EQ(Status::OK, db->CreateJournal(JournalType::EXPLICIT, commit_id,
                                          &explicit_journal));

  EXPECT_EQ(Status::OK, db->RemoveExplicitJournals());

  // Removing explicit journals should not affect the implicit ones.
  std::vector<JournalId> journal_ids;
  EXPECT_EQ(Status::OK, db->GetImplicitJournalIds(&journal_ids));
  EXPECT_EQ(1u, journal_ids.size());

  std::unique_ptr<Journal> found_journal;
  EXPECT_EQ(Status::OK, db->GetImplicitJournal(journal_ids[0], &found_journal));
  EXPECT_EQ(Status::OK, db->RemoveJournal(journal_ids[0]));
  EXPECT_EQ(Status::NOT_FOUND,
            db->GetImplicitJournal(journal_ids[0], &found_journal));
  EXPECT_EQ(Status::OK, db->GetImplicitJournalIds(&journal_ids));
  EXPECT_EQ(0u, journal_ids.size());
}

TEST_F(DBTest, JournalEntries) {
  std::unique_ptr<DB> db = GetDB();
  CommitId commit_id = RandomId(kCommitIdSize);

  std::unique_ptr<Journal> implicit_journal;
  EXPECT_EQ(Status::OK, db->CreateJournal(JournalType::IMPLICIT, commit_id,
                                          &implicit_journal));
  EXPECT_EQ(Status::OK,
            implicit_journal->Put("add-key-1", "value1", KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            implicit_journal->Put("add-key-2", "value2", KeyPriority::EAGER));
  EXPECT_EQ(Status::OK,
            implicit_journal->Put("add-key-1", "value3", KeyPriority::LAZY));
  EXPECT_EQ(Status::OK, implicit_journal->Delete("remove-key"));

  EntryChange expected_changes[] = {
      NewEntryChange("add-key-1", "value3", KeyPriority::LAZY),
      NewEntryChange("add-key-2", "value2", KeyPriority::EAGER),
      NewRemoveEntryChange("remove-key"),
  };
  std::unique_ptr<Iterator<const EntryChange>> entries;
  EXPECT_EQ(Status::OK,
            db->GetJournalEntries(
                static_cast<JournalDBImpl*>(implicit_journal.get())->GetId(),
                &entries));
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(entries->Valid());
    ExpectChangesEqual(expected_changes[i], **entries);
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
  EXPECT_EQ(Status::OK, entries->GetStatus());
}

TEST_F(DBTest, UnsyncedCommits) {
  std::unique_ptr<DB> db = GetDB();

  CommitId commit_id = RandomId(kCommitIdSize);
  std::vector<CommitId> commit_ids;
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commit_ids));
  EXPECT_TRUE(commit_ids.empty());

  EXPECT_EQ(Status::OK, db->MarkCommitIdUnsynced(commit_id));
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commit_ids));
  EXPECT_EQ(1u, commit_ids.size());
  EXPECT_EQ(commit_id, commit_ids[0]);
  bool is_synced;
  EXPECT_EQ(Status::OK, db->IsCommitSynced(commit_id, &is_synced));
  EXPECT_FALSE(is_synced);

  EXPECT_EQ(Status::OK, db->MarkCommitIdSynced(commit_id));
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commit_ids));
  EXPECT_TRUE(commit_ids.empty());
  EXPECT_EQ(Status::OK, db->IsCommitSynced(commit_id, &is_synced));
  EXPECT_TRUE(is_synced);
}

TEST_F(DBTest, UnsyncedObjects) {
  std::unique_ptr<DB> db = GetDB();

  ObjectId object_id = RandomId(kObjectIdSize);
  std::vector<ObjectId> object_ids;
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&object_ids));
  EXPECT_TRUE(object_ids.empty());

  EXPECT_EQ(Status::OK, db->MarkObjectIdUnsynced(object_id));
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&object_ids));
  EXPECT_EQ(1u, object_ids.size());
  EXPECT_EQ(object_id, object_ids[0]);
  bool is_synced;
  EXPECT_EQ(Status::OK, db->IsObjectSynced(object_id, &is_synced));
  EXPECT_FALSE(is_synced);

  EXPECT_EQ(Status::OK, db->MarkObjectIdSynced(object_id));
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&object_ids));
  EXPECT_TRUE(object_ids.empty());
  EXPECT_EQ(Status::OK, db->IsObjectSynced(object_id, &is_synced));
  EXPECT_TRUE(is_synced);
}

TEST_F(DBTest, Batch) {
  std::unique_ptr<DB> db = GetDB();
  std::unique_ptr<DB::Batch> batch = db->StartBatch();

  ObjectId object_id = RandomId(kObjectIdSize);
  EXPECT_EQ(Status::OK, db->MarkObjectIdUnsynced(object_id));

  std::vector<ObjectId> object_ids;
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&object_ids));
  EXPECT_TRUE(object_ids.empty());

  EXPECT_EQ(Status::OK, batch->Execute());

  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&object_ids));
  EXPECT_EQ(1u, object_ids.size());
  EXPECT_EQ(object_id, object_ids[0]);
}

}  // namespace
}  // namespace storage
