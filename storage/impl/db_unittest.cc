// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/db.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/public/constants.h"
#include "apps/ledger/storage/impl/journal_db_impl.h"
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

EntryChange newEntryChange(std::string key,
                           std::string blob_id,
                           KeyPriority priority) {
  EntryChange change;
  change.deleted = false;
  change.entry.key.swap(key);
  change.entry.blob_id.swap(blob_id);
  change.entry.priority = priority;
  return change;
}

EntryChange newRemoveEntryChange(std::string key) {
  EntryChange change;
  change.deleted = true;
  change.entry.key.swap(key);
  return change;
}

void ExpectChangesEqual(const EntryChange& expected, const EntryChange& found) {
  EXPECT_EQ(expected.deleted, found.deleted);
  EXPECT_EQ(expected.entry.key, found.entry.key);
  if (!expected.deleted) {
    // If the entry is deleted, blob_id and priority are not valid.
    EXPECT_EQ(expected.entry.blob_id, found.entry.blob_id);
    EXPECT_EQ(expected.entry.priority, found.entry.priority);
  }
}

}  // namespace

class DBTest : public ::testing::Test {
 public:
  DBTest() {}

  ~DBTest() override {}

  // Test:
  void SetUp() override { std::srand(0); }

 protected:
  std::unique_ptr<DB> GetDB() {
    std::unique_ptr<DB> db = std::unique_ptr<DB>(new DB(tmp_dir_.path()));
    EXPECT_EQ(Status::OK, db->Init());
    return db;
  }

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

  std::unique_ptr<Commit> storedCommit;
  std::string storageBytes;
  CommitImpl commit(RandomId(kCommitIdSize), 123, RandomId(kObjectIdSize),
                    parents);

  EXPECT_EQ(Status::NOT_FOUND,
            db->GetCommitStorageBytes(commit.GetId(), &storageBytes));

  EXPECT_EQ(Status::OK, db->AddCommitStorageBytes(commit.GetId(),
                                                  commit.GetStorageBytes()));
  EXPECT_EQ(Status::OK,
            db->GetCommitStorageBytes(commit.GetId(), &storageBytes));
  EXPECT_EQ(storageBytes, commit.GetStorageBytes());

  EXPECT_EQ(Status::OK, db->RemoveCommit(commit.GetId()));
  EXPECT_EQ(Status::NOT_FOUND,
            db->GetCommitStorageBytes(commit.GetId(), &storageBytes));
}

TEST_F(DBTest, Journals) {
  std::unique_ptr<DB> db = GetDB();
  CommitId commitId = RandomId(kCommitIdSize);

  std::unique_ptr<Journal> implicitJournal;
  std::unique_ptr<Journal> explicitJournal;
  EXPECT_EQ(Status::OK, db->CreateJournal(true, commitId, &implicitJournal));
  EXPECT_EQ(Status::OK, db->CreateJournal(false, commitId, &explicitJournal));

  std::vector<JournalId> journalIds;
  EXPECT_EQ(Status::OK, db->GetImplicitJournalIds(&journalIds));
  EXPECT_EQ(1u, journalIds.size());

  std::unique_ptr<Journal> foundJournal;
  EXPECT_EQ(Status::OK, db->GetImplicitJournal(journalIds[0], &foundJournal));
  EXPECT_EQ(Status::OK, db->RemoveJournal(journalIds[0]));
  EXPECT_EQ(Status::NOT_FOUND,
            db->GetImplicitJournal(journalIds[0], &foundJournal));
  EXPECT_EQ(Status::OK, db->GetImplicitJournalIds(&journalIds));
  EXPECT_EQ(0u, journalIds.size());

  EXPECT_EQ(Status::OK, db->RemoveExplicitJournals());
}

TEST_F(DBTest, JournalEntries) {
  std::unique_ptr<DB> db = GetDB();
  CommitId commitId = RandomId(kCommitIdSize);

  std::unique_ptr<Journal> implicitJournal;
  EXPECT_EQ(Status::OK, db->CreateJournal(true, commitId, &implicitJournal));
  EXPECT_EQ(Status::OK,
            implicitJournal->Put("add-key-1", "value1", KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            implicitJournal->Put("add-key-2", "value2", KeyPriority::EAGER));
  EXPECT_EQ(Status::OK,
            implicitJournal->Put("add-key-1", "value3", KeyPriority::LAZY));
  EXPECT_EQ(Status::OK, implicitJournal->Delete("remove-key"));

  EntryChange expectedChanges[] = {
      newEntryChange("add-key-1", "value3", KeyPriority::LAZY),
      newEntryChange("add-key-2", "value2", KeyPriority::EAGER),
      newRemoveEntryChange("remove-key"),
  };
  std::unique_ptr<Iterator<const EntryChange>> entries;
  EXPECT_EQ(Status::OK,
            db->GetJournalEntries(
                static_cast<JournalDBImpl*>(implicitJournal.get())->GetId(),
                &entries));
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(entries->Done());
    ExpectChangesEqual(expectedChanges[i], **entries);
    entries->Next();
  }
  EXPECT_TRUE(entries->Done());
}

TEST_F(DBTest, UnsyncedCommits) {
  std::unique_ptr<DB> db = GetDB();

  CommitId commitId = RandomId(kCommitIdSize);
  std::vector<CommitId> commitIds;
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commitIds));
  EXPECT_TRUE(commitIds.empty());

  EXPECT_EQ(Status::OK, db->MarkCommitIdUnsynced(commitId));
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commitIds));
  EXPECT_EQ(1u, commitIds.size());
  EXPECT_EQ(commitId, commitIds[0]);
  bool isSynced;
  EXPECT_EQ(Status::OK, db->IsCommitSynced(commitId, &isSynced));
  EXPECT_FALSE(isSynced);

  EXPECT_EQ(Status::OK, db->MarkCommitIdSynced(commitId));
  EXPECT_EQ(Status::OK, db->GetUnsyncedCommitIds(&commitIds));
  EXPECT_TRUE(commitIds.empty());
  EXPECT_EQ(Status::OK, db->IsCommitSynced(commitId, &isSynced));
  EXPECT_TRUE(isSynced);
}

TEST_F(DBTest, UnsyncedObjects) {
  std::unique_ptr<DB> db = GetDB();

  ObjectId objectId = RandomId(kObjectIdSize);
  std::vector<ObjectId> objectIds;
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&objectIds));
  EXPECT_TRUE(objectIds.empty());

  EXPECT_EQ(Status::OK, db->MarkObjectIdUnsynced(objectId));
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&objectIds));
  EXPECT_EQ(1u, objectIds.size());
  EXPECT_EQ(objectId, objectIds[0]);
  bool isSynced;
  EXPECT_EQ(Status::OK, db->IsObjectSynced(objectId, &isSynced));
  EXPECT_FALSE(isSynced);

  EXPECT_EQ(Status::OK, db->MarkObjectIdSynced(objectId));
  EXPECT_EQ(Status::OK, db->GetUnsyncedObjectIds(&objectIds));
  EXPECT_TRUE(objectIds.empty());
  EXPECT_EQ(Status::OK, db->IsObjectSynced(objectId, &isSynced));
  EXPECT_TRUE(isSynced);
}

}  // namespace storage
