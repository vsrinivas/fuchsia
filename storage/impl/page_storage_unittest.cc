// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

#include <memory>

#include "apps/ledger/glue/crypto/hash.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/impl/journal_db_impl.h"
#include "apps/ledger/storage/public/commit_watcher.h"
#include "apps/ledger/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

std::string ToHex(const std::string& string) {
  std::string result;
  for (char c : string) {
    if (c >= 0 && c < 16)
      result += "0";
    result += NumberToString(static_cast<uint8_t>(c), ftl::Base::k16);
  }
  return result;
}

class FakeCommitWatcher : public CommitWatcher {
 public:
  FakeCommitWatcher() {}

  void OnNewCommit(const Commit& commit, ChangeSource source) override {
    ++commit_count;
    last_commit_id = commit.GetId();
    last_source = source;
  }

  int commit_count = 0;
  CommitId last_commit_id;
  ChangeSource last_source;
};

// Only implements |Init()|, |CreateJournal() and |CreateMergeJournal()| and
// returns an |IO_ERROR| in all other cases.
class FakeDbImpl : public DB {
 public:
  FakeDbImpl(PageStorageImpl* page_storage) : page_storage_(page_storage) {}

  Status Init() override { return Status::OK; }
  Status CreateJournal(JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override {
    JournalId id = RandomId(10);
    *journal =
        JournalDBImpl::Simple(journal_type, page_storage_, this, id, base);
    return Status::OK;
  }
  Status CreateMergeJournal(const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override {
    *journal =
        JournalDBImpl::Merge(page_storage_, this, RandomId(10), base, other);
    return Status::OK;
  }

  std::unique_ptr<Batch> StartBatch() override { return nullptr; }
  Status GetHeads(std::vector<CommitId>* heads) override {
    return Status::IO_ERROR;
  }
  Status AddHead(const CommitId& head) override { return Status::IO_ERROR; }
  Status RemoveHead(const CommitId& head) override { return Status::IO_ERROR; }
  Status ContainsHead(const CommitId& commit_id) override {
    return Status::IO_ERROR;
  }
  Status GetCommitStorageBytes(const CommitId& commit_id,
                               std::string* storage_bytes) override {
    return Status::IO_ERROR;
  }
  Status AddCommitStorageBytes(const CommitId& commit_id,
                               const std::string& storage_bytes) override {
    return Status::IO_ERROR;
  }
  Status RemoveCommit(const CommitId& commit_id) override {
    return Status::IO_ERROR;
  }
  Status GetImplicitJournalIds(std::vector<JournalId>* journal_ids) override {
    return Status::IO_ERROR;
  }
  Status GetImplicitJournal(const JournalId& journal_id,
                            std::unique_ptr<Journal>* journal) override {
    return Status::IO_ERROR;
  }
  Status RemoveExplicitJournals() override { return Status::IO_ERROR; }
  Status RemoveJournal(const JournalId& journal_id) override {
    return Status::IO_ERROR;
  }
  Status AddJournalEntry(const JournalId& journal_id,
                         ftl::StringView key,
                         ftl::StringView value,
                         KeyPriority priority) override {
    return Status::IO_ERROR;
  }
  Status RemoveJournalEntry(const JournalId& journal_id,
                            convert::ExtendedStringView key) override {
    return Status::IO_ERROR;
  }
  Status GetJournalEntries(
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) override {
    return Status::IO_ERROR;
  }
  Status GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) override {
    return Status::IO_ERROR;
  }
  Status MarkCommitIdSynced(const CommitId& commit_id) override {
    return Status::IO_ERROR;
  }
  Status MarkCommitIdUnsynced(const CommitId& commit_id) override {
    return Status::IO_ERROR;
  }
  Status IsCommitSynced(const CommitId& commit_id, bool* is_synced) override {
    return Status::IO_ERROR;
  }
  Status GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids) override {
    return Status::IO_ERROR;
  }
  Status MarkObjectIdSynced(ObjectIdView object_id) override {
    return Status::IO_ERROR;
  }
  Status MarkObjectIdUnsynced(ObjectIdView object_id) override {
    return Status::IO_ERROR;
  }
  Status IsObjectSynced(ObjectIdView object_id, bool* is_synced) override {
    return Status::IO_ERROR;
  }
  Status SetNodeSize(size_t node_size) override { return Status::IO_ERROR; }
  Status GetNodeSize(size_t* node_size) override { return Status::IO_ERROR; }

 private:
  PageStorageImpl* page_storage_;
};

class PageStorageTest : public ::testing::Test {
 public:
  PageStorageTest() {}

  ~PageStorageTest() override {}

  // Test:
  void SetUp() override {
    std::srand(0);
    PageId id = RandomId(16);
    storage_.reset(
        new PageStorageImpl(message_loop_.task_runner(), tmp_dir_.path(), id));
    EXPECT_EQ(Status::OK, storage_->Init());
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  CommitId GetFirstHead() {
    std::vector<CommitId> ids;
    EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&ids));
    EXPECT_FALSE(ids.empty());
    return ids[0];
  }

  CommitId TryCommitFromSync() {
    std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
    CommitId id = commit->GetId();

    EXPECT_EQ(Status::OK,
              storage_->AddCommitFromSync(id, commit->GetStorageBytes()));
    return id;
  }

  CommitId TryCommitFromLocal(JournalType type, int keys) {
    std::unique_ptr<Journal> journal;
    EXPECT_EQ(Status::OK,
              storage_->StartCommit(GetFirstHead(), type, &journal));
    EXPECT_NE(nullptr, journal);

    for (int i = 0; i < keys; ++i) {
      EXPECT_EQ(Status::OK,
                journal->Put("key" + std::to_string(i), RandomId(kObjectIdSize),
                             KeyPriority::EAGER));
    }
    EXPECT_EQ(Status::OK, journal->Delete("key_does_not_exist"));

    ObjectId commit_id;
    journal->Commit([this, &commit_id](Status status, const CommitId& id) {
      EXPECT_EQ(Status::OK, status);
      commit_id = id;
    });

    // Commit and Rollback should fail after a successfull commit.
    journal->Commit([this](Status status, const CommitId&) {
      EXPECT_EQ(Status::ILLEGAL_STATE, status);
    });
    EXPECT_EQ(Status::ILLEGAL_STATE, journal->Rollback());

    // Check the contents.
    std::unique_ptr<Commit> commit;
    EXPECT_EQ(Status::OK, storage_->GetCommit(commit_id, &commit));
    std::unique_ptr<Iterator<const Entry>> contents =
        commit->GetContents()->begin();
    for (int i = 0; i < keys; ++i) {
      EXPECT_TRUE(contents->Valid());
      EXPECT_EQ("key" + std::to_string(i), (*contents)->key);
      contents->Next();
    }
    EXPECT_FALSE(contents->Valid());

    return commit_id;
  }

  mtl::MessageLoop message_loop_;
  files::ScopedTempDir tmp_dir_;

 protected:
  std::unique_ptr<PageStorageImpl> storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorageTest);
};

TEST_F(PageStorageTest, AddGetLocalCommits) {
  // Search for a commit id that doesn't exist and see the error.
  std::unique_ptr<Commit> commit;
  EXPECT_EQ(Status::NOT_FOUND,
            storage_->GetCommit(RandomId(kCommitIdSize), &commit));
  EXPECT_FALSE(commit);

  commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes();

  // Search for a commit that exist and check the content.
  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  std::unique_ptr<Commit> found;
  EXPECT_EQ(Status::OK, storage_->GetCommit(id, &found));
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddGetSyncedCommits) {
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
  CommitId id = commit->GetId();

  EXPECT_EQ(Status::OK,
            storage_->AddCommitFromSync(id, commit->GetStorageBytes()));

  std::unique_ptr<Commit> found;
  EXPECT_EQ(Status::OK, storage_->GetCommit(id, &found));
  EXPECT_EQ(commit->GetStorageBytes(), found->GetStorageBytes());

  // Check that the commit is not marked as unsynced.
  std::vector<std::unique_ptr<const Commit>> commits;
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<const Commit>> commits;

  // Initially there should be no unsynced commits.
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());

  // After adding a commit it should marked as unsynced.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes();

  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(storage_bytes, commits[0]->GetStorageBytes());

  // Mark it as synced.
  EXPECT_EQ(Status::OK, storage_->MarkCommitSynced(id));
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, HeadCommits) {
  // Every page should have one initial head commit.
  std::vector<CommitId> heads;
  EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&heads));
  EXPECT_EQ(1u, heads.size());

  // Adding a new commit with the previous head as its parent should replace the
  // old head.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
  CommitId id = commit->GetId();

  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&heads));
  EXPECT_EQ(1u, heads.size());
  EXPECT_EQ(id, heads[0]);
}

TEST_F(PageStorageTest, CreateJournals) {
  // Explicit journal.
  CommitId left_id = TryCommitFromLocal(JournalType::EXPLICIT, 5);
  CommitId right_id = TryCommitFromLocal(JournalType::IMPLICIT, 10);

  // Journal for merge commit.
  std::unique_ptr<Journal> journal;
  EXPECT_EQ(Status::OK,
            storage_->StartMergeCommit(left_id, right_id, &journal));
  EXPECT_EQ(Status::OK, journal->Rollback());
  EXPECT_NE(nullptr, journal);
}

TEST_F(PageStorageTest, JournalCommitFailsAfterFailedOperation) {
  FakeDbImpl db(storage_.get());

  std::unique_ptr<Journal> journal;
  // Explicit journals.
  // The first call will fail because FakeDBImpl::AddJournalEntry() returns an
  // IO_ERROR. After a failed call all other Put/Delete/Commit operations should
  // fail with ILLEGAL_STATE. Rollback will fail with IO_ERROR because
  // FakeDBImpl::RemoveJournal() returns it.
  db.CreateJournal(JournalType::EXPLICIT, RandomId(kCommitIdSize), &journal);
  EXPECT_EQ(Status::IO_ERROR, journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::ILLEGAL_STATE,
            journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::ILLEGAL_STATE, journal->Delete("key"));
  journal->Commit([](Status s, const CommitId& id) {
    EXPECT_EQ(Status::ILLEGAL_STATE, s);
  });
  EXPECT_EQ(Status::IO_ERROR, journal->Rollback());

  // Implicit journals.
  // All calls will fail because of FakeDBImpl implementation, not because of
  // ILLEGAL_STATE.
  db.CreateJournal(JournalType::IMPLICIT, RandomId(kCommitIdSize), &journal);
  EXPECT_EQ(Status::IO_ERROR, journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::IO_ERROR, journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::IO_ERROR, journal->Delete("key"));
  journal->Commit(
      [](Status s, const CommitId& id) { EXPECT_EQ(Status::IO_ERROR, s); });
  EXPECT_EQ(Status::IO_ERROR, journal->Rollback());
}

TEST_F(PageStorageTest, DestroyUncommittedJournal) {
  // It is not an error if a journal is not committed or rolled back.
  std::unique_ptr<Journal> journal;
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::EXPLICIT, &journal));
  EXPECT_NE(nullptr, journal);
  EXPECT_EQ(Status::OK,
            journal->Put("key", RandomId(kObjectIdSize), KeyPriority::EAGER));
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  std::string content("Some data");

  ObjectId object_id;
  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(content), content.size(),
      [this, &object_id](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        object_id = std::move(returned_object_id);
        message_loop_.QuitNow();
      });
  message_loop_.Run();

  std::string hash = glue::SHA256Hash(content.data(), content.size());
  EXPECT_EQ(hash, object_id);

  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(content, file_content);
}

TEST_F(PageStorageTest, AddObjectFromLocalNegativeSize) {
  std::string content("Some data");
  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(content), -1,
      [this](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        message_loop_.QuitNow();
      });
  message_loop_.Run();
}

TEST_F(PageStorageTest, AddObjectFromLocalWrongSize) {
  std::string content("Some data");

  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(content), 123,
      [this](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::IO_ERROR, returned_status);
        message_loop_.QuitNow();
      });
  message_loop_.Run();
}

TEST_F(PageStorageTest, GetObject) {
  std::string content("Some data");
  ObjectId object_id = glue::SHA256Hash(content.data(), content.size());
  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  ASSERT_TRUE(files::WriteFile(file_path, content.data(), content.size()));

  Status status;
  std::unique_ptr<const Object> object;
  storage_->GetObject(
      object_id,
      [this, &status, &object](Status returned_status,
                               std::unique_ptr<const Object> returned_object) {
        status = returned_status;
        object = std::move(returned_object);
        message_loop_.QuitNow();
      });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(object_id, object->GetId());
  ftl::StringView data;
  ASSERT_EQ(Status::OK, object->GetData(&data));
  EXPECT_EQ(content, convert::ToString(data));
}

TEST_F(PageStorageTest, AddObjectSynchronous) {
  std::string content("Some data");

  std::unique_ptr<const Object> object;
  Status status = storage_->AddObjectSynchronous(content, &object);
  EXPECT_EQ(Status::OK, status);
  std::string hash = glue::SHA256Hash(content.data(), content.size());
  EXPECT_EQ(hash, object->GetId());

  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(hash);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(content, file_content);
}

TEST_F(PageStorageTest, GetObjectSynchronous) {
  std::string content("Some data");
  ObjectId object_id = glue::SHA256Hash(content.data(), content.size());
  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  ASSERT_TRUE(files::WriteFile(file_path, content.data(), content.size()));

  std::unique_ptr<const Object> object;
  Status status = storage_->GetObjectSynchronous(object_id, &object);

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(object_id, object->GetId());
  ftl::StringView data;
  ASSERT_EQ(Status::OK, object->GetData(&data));
  EXPECT_EQ(content, convert::ToString(data));
}

TEST_F(PageStorageTest, CommitWatchers) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  // Add a watcher and receive the commit.
  CommitId expected = TryCommitFromLocal(JournalType::EXPLICIT, 10);
  EXPECT_EQ(1, watcher.commit_count);
  EXPECT_EQ(expected, watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);

  // Add a second watcher.
  FakeCommitWatcher watcher2;
  storage_->AddCommitWatcher(&watcher2);
  expected = TryCommitFromLocal(JournalType::IMPLICIT, 10);
  EXPECT_EQ(2, watcher.commit_count);
  EXPECT_EQ(expected, watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);
  EXPECT_EQ(1, watcher2.commit_count);
  EXPECT_EQ(expected, watcher2.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher2.last_source);

  // Remove one watcher.
  storage_->RemoveCommitWatcher(&watcher2);
  expected = TryCommitFromSync();
  EXPECT_EQ(3, watcher.commit_count);
  EXPECT_EQ(expected, watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::SYNC, watcher.last_source);
  EXPECT_EQ(1, watcher2.commit_count);
}

}  // namespace
}  // namespace storage
