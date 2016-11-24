// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_storage_impl.h"

#include <memory>

#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/db_empty_impl.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {

class PageStorageImplAccessorForTest {
 public:
  static std::string GetFilePath(const PageStorageImpl& storage,
                                 ObjectIdView object_id) {
    return storage.GetFilePath(object_id);
  }
};

namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
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

class FakeSyncDelegate : public PageSyncDelegate {
 public:
  void AddObject(ObjectIdView object_id, const std::string& value) {
    id_to_value_[object_id.ToString()] = value;
  }

  void GetObject(ObjectIdView object_id,
                 std::function<void(Status status,
                                    uint64_t size,
                                    mx::datapipe_consumer data)> callback) {
    std::string id = object_id.ToString();
    std::string& value = id_to_value_[id];
    object_requests.insert(id);
    callback(Status::OK, value.size(), mtl::WriteStringToConsumerHandle(value));
  }

  std::set<ObjectId> object_requests;

 private:
  std::map<ObjectId, std::string> id_to_value_;
};

// Implements |Init()|, |CreateJournal() and |CreateMergeJournal()| and
// fails with a |NOT_IMPLEMENTED| error in all other cases.
class FakeDbImpl : public DbEmptyImpl {
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

 private:
  PageStorageImpl* page_storage_;
};

class ObjectData {
 public:
  ObjectData(const std::string& value)
      : value(value),
        size(value.size()),
        object_id(glue::SHA256Hash(value.data(), value.size())) {}
  const std::string value;
  const size_t size;
  const std::string object_id;
};

class PageStorageTest : public test::TestWithMessageLoop {
 public:
  PageStorageTest() {}

  ~PageStorageTest() override {}

  // Test:
  void SetUp() override {
    PageId id = RandomId(16);
    storage_ = std::make_unique<PageStorageImpl>(message_loop_.task_runner(),
                                                 tmp_dir_.path(), id);
    EXPECT_EQ(Status::OK, storage_->Init());
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  std::string GetFilePath(ObjectIdView object_id) {
    return PageStorageImplAccessorForTest::GetFilePath(*storage_, object_id);
  }

  CommitId GetFirstHead() {
    std::vector<CommitId> ids;
    EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&ids));
    EXPECT_FALSE(ids.empty());
    return ids[0];
  }

  CommitId TryCommitFromSync() {
    ObjectId root_id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(storage_.get(), std::vector<Entry>(),
                                    std::vector<ObjectId>(1), &root_id));
    std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), root_id, {GetFirstHead()});
    CommitId id = commit->GetId();

    storage_->AddCommitFromSync(id, commit->GetStorageBytes(),
                                [this](Status status) {
                                  EXPECT_EQ(Status::OK, status);
                                  message_loop_.PostQuitTask();
                                });
    EXPECT_FALSE(RunLoopWithTimeout());
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
    std::unique_ptr<const Commit> commit;
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

  void TryAddFromLocal(const std::string& content,
                       const ObjectId& expected_id) {
    storage_->AddObjectFromLocal(
        mtl::WriteStringToConsumerHandle(content), content.size(),
        [this, &expected_id](Status returned_status, ObjectId object_id) {
          EXPECT_EQ(Status::OK, returned_status);
          EXPECT_EQ(expected_id, object_id);
          message_loop_.PostQuitTask();
        });
    message_loop_.Run();
  }

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<PageStorageImpl> storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorageTest);
};

TEST_F(PageStorageTest, AddGetLocalCommits) {
  // Search for a commit id that doesn't exist and see the error.
  std::unique_ptr<const Commit> lookup_commit;
  EXPECT_EQ(Status::NOT_FOUND,
            storage_->GetCommit(RandomId(kCommitIdSize), &lookup_commit));
  EXPECT_FALSE(lookup_commit);

  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {GetFirstHead()});
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes();

  // Search for a commit that exist and check the content.
  storage_->AddCommitFromLocal(
      std::move(commit), [](Status status) { EXPECT_EQ(Status::OK, status); });
  std::unique_ptr<const Commit> found;
  EXPECT_EQ(Status::OK, storage_->GetCommit(id, &found));
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddCommitBeforeParentsError) {
  // Try to add a commit before its parent and see the error.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomId(kObjectIdSize), {RandomId(kCommitIdSize)});

  storage_->AddCommitFromLocal(std::move(commit), [](Status status) {
    EXPECT_EQ(Status::ILLEGAL_STATE, status);
  });
}

TEST_F(PageStorageTest, AddGetSyncedCommits) {
  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);

  // Create a node with 2 values.
  ObjectData lazy_value("Some data");
  ObjectData eager_value("More data");
  std::vector<Entry> entries = {
      Entry{"key0", lazy_value.object_id, storage::KeyPriority::LAZY},
      Entry{"key1", eager_value.object_id, storage::KeyPriority::EAGER},
  };
  ObjectId root_id;
  ASSERT_EQ(Status::OK,
            TreeNode::FromEntries(storage_.get(), entries,
                                  std::vector<ObjectId>(entries.size() + 1),
                                  &root_id));

  // Add the three objects to FakeSyncDelegate.
  sync.AddObject(lazy_value.object_id, lazy_value.value);
  sync.AddObject(eager_value.object_id, eager_value.value);
  std::unique_ptr<const Object> root_object;
  ASSERT_EQ(Status::OK, storage_->GetObjectSynchronous(root_id, &root_object));
  ftl::StringView root_data;
  ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
  sync.AddObject(root_id, root_data.ToString());

  // Remove the root from the local storage. The two values were never added.
  std::string file_path = GetFilePath(root_id);
  files::DeletePath(file_path, false);

  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), root_id, {GetFirstHead()});
  CommitId id = commit->GetId();

  // Adding the commit should only request the tree node and the eager value.
  sync.object_requests.clear();
  storage_->AddCommitFromSync(id, commit->GetStorageBytes(),
                              [this](Status status) {
                                EXPECT_EQ(Status::OK, status);
                                message_loop_.PostQuitTask();
                              });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, sync.object_requests.size());
  EXPECT_TRUE(sync.object_requests.find(root_id) != sync.object_requests.end());
  EXPECT_TRUE(sync.object_requests.find(eager_value.object_id) !=
              sync.object_requests.end());

  // Adding the same commit twice should not request any objects from sync.
  sync.object_requests.clear();
  storage_->AddCommitFromSync(id, commit->GetStorageBytes(),
                              [this](Status status) {
                                EXPECT_EQ(Status::OK, status);
                                message_loop_.PostQuitTask();
                              });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(sync.object_requests.empty());

  std::unique_ptr<const Commit> found;
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

  storage_->AddCommitFromLocal(
      std::move(commit), [](Status status) { EXPECT_EQ(Status::OK, status); });
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

  storage_->AddCommitFromLocal(
      std::move(commit), [](Status status) { EXPECT_EQ(Status::OK, status); });
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
  // error. After a failed call all other Put/Delete/Commit operations should
  // fail with ILLEGAL_STATE. Rollback should not fail with ILLEGAL_STATE.
  db.CreateJournal(JournalType::EXPLICIT, RandomId(kCommitIdSize), &journal);
  EXPECT_NE(Status::OK, journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::ILLEGAL_STATE,
            journal->Put("key", "value", KeyPriority::EAGER));
  EXPECT_EQ(Status::ILLEGAL_STATE, journal->Delete("key"));
  journal->Commit([](Status s, const CommitId& id) {
    EXPECT_EQ(Status::ILLEGAL_STATE, s);
  });
  EXPECT_NE(Status::ILLEGAL_STATE, journal->Rollback());

  // Implicit journals.
  // All calls will fail because of FakeDBImpl implementation, not because of
  // an ILLEGAL_STATE error.
  db.CreateJournal(JournalType::IMPLICIT, RandomId(kCommitIdSize), &journal);
  EXPECT_NE(Status::OK, journal->Put("key", "value", KeyPriority::EAGER));
  Status put_status = journal->Put("key", "value", KeyPriority::EAGER);
  EXPECT_NE(Status::ILLEGAL_STATE, put_status);
  EXPECT_NE(Status::ILLEGAL_STATE, journal->Delete("key"));
  journal->Commit([](Status s, const CommitId& id) {
    EXPECT_NE(Status::ILLEGAL_STATE, s);
  });
  EXPECT_NE(Status::ILLEGAL_STATE, journal->Rollback());
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
  ObjectData data("Some data");

  ObjectId object_id;
  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(data.value), data.size,
      [this, &object_id](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        object_id = std::move(returned_object_id);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();

  EXPECT_EQ(data.object_id, object_id);

  std::string file_path = GetFilePath(object_id);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(data.value, file_content);
  EXPECT_TRUE(storage_->ObjectIsUntracked(object_id));
}

TEST_F(PageStorageTest, AddObjectFromLocalNegativeSize) {
  ObjectData data("Some data");

  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(data.value), -1,
      [this](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, AddObjectFromLocalWrongSize) {
  ObjectData data("Some data");

  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(data.value), 123,
      [this](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::IO_ERROR, returned_status);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, AddObjectFromSync) {
  ObjectData data("Some data");

  storage_->AddObjectFromSync(data.object_id,
                              mtl::WriteStringToConsumerHandle(data.value),
                              data.size, [this](Status returned_status) {
                                EXPECT_EQ(Status::OK, returned_status);
                                message_loop_.PostQuitTask();
                              });
  message_loop_.Run();

  std::string file_path = GetFilePath(data.object_id);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(data.value, file_content);
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, AddObjectFromSyncWrongObjectId) {
  ObjectData data("Some data");
  ObjectId wrong_id = RandomId(kObjectIdSize);

  storage_->AddObjectFromSync(
      wrong_id, mtl::WriteStringToConsumerHandle(data.value), data.size,
      [this](Status returned_status) {
        EXPECT_EQ(Status::OBJECT_ID_MISMATCH, returned_status);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
}

TEST_F(PageStorageTest, AddObjectFromSyncWrongSize) {
  ObjectData data("Some data");

  storage_->AddObjectFromSync(data.object_id,
                              mtl::WriteStringToConsumerHandle(data.value), 123,
                              [this](Status returned_status) {
                                EXPECT_EQ(Status::IO_ERROR, returned_status);
                                message_loop_.PostQuitTask();
                              });
  message_loop_.Run();
}

TEST_F(PageStorageTest, GetObject) {
  ObjectData data("Some data");
  std::string file_path = GetFilePath(data.object_id);
  ASSERT_TRUE(files::WriteFile(file_path, data.value.data(), data.size));

  Status status;
  std::unique_ptr<const Object> object;
  storage_->GetObject(
      data.object_id,
      [this, &status, &object](Status returned_status,
                               std::unique_ptr<const Object> returned_object) {
        status = returned_status;
        object = std::move(returned_object);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_id, object->GetId());
  ftl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data.value, convert::ToString(object_data));
}

TEST_F(PageStorageTest, GetObjectFromSync) {
  ObjectData data("Some data");
  FakeSyncDelegate sync;
  sync.AddObject(data.object_id, data.value);
  storage_->SetSyncDelegate(&sync);

  Status status;
  std::unique_ptr<const Object> object;
  storage_->GetObject(
      data.object_id,
      [this, &status, &object](Status returned_status,
                               std::unique_ptr<const Object> returned_object) {
        status = returned_status;
        object = std::move(returned_object);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_id, object->GetId());
  ftl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data.value, convert::ToString(object_data));

  storage_->SetSyncDelegate(nullptr);
  storage_->GetObject(RandomId(kObjectIdSize),
                      [this](Status returned_status,
                             std::unique_ptr<const Object> returned_object) {
                        EXPECT_EQ(Status::NOT_CONNECTED_ERROR, returned_status);
                        EXPECT_EQ(nullptr, returned_object);
                      });
}

TEST_F(PageStorageTest, AddObjectSynchronous) {
  ObjectData data("Some data");

  std::unique_ptr<const Object> object;
  Status status = storage_->AddObjectSynchronous(data.value, &object);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_id, object->GetId());

  std::string file_path = GetFilePath(data.object_id);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(data.value, file_content);
}

TEST_F(PageStorageTest, GetObjectSynchronous) {
  ObjectData data("Some data");
  std::string file_path = GetFilePath(data.object_id);
  ASSERT_TRUE(files::WriteFile(file_path, data.value.data(), data.size));

  std::unique_ptr<const Object> object;
  Status status = storage_->GetObjectSynchronous(data.object_id, &object);

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_id, object->GetId());
  ftl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data.value, convert::ToString(object_data));
}

TEST_F(PageStorageTest, UnsyncedObjects) {
  int size = 3;
  ObjectData data[] = {
      ObjectData("Some data"), ObjectData("Some more data"),
      ObjectData("Even more data"),
  };
  for (int i = 0; i < size; ++i) {
    TryAddFromLocal(data[i].value, data[i].object_id);
    EXPECT_TRUE(storage_->ObjectIsUntracked(data[i].object_id));
  }

  std::vector<CommitId> commits;

  // Add one key-value pair per commit.
  for (int i = 0; i < size; ++i) {
    std::unique_ptr<Journal> journal;
    EXPECT_EQ(Status::OK, storage_->StartCommit(
                              GetFirstHead(), JournalType::IMPLICIT, &journal));
    EXPECT_EQ(Status::OK, journal->Put("key" + ftl::NumberToString(i),
                                       data[i].object_id, KeyPriority::LAZY));
    journal->Commit([](Status status, const CommitId& id) {
      EXPECT_EQ(Status::OK, status);
    });
    commits.push_back(GetFirstHead());
  }

  // Without syncing anything, the unsynced objects of any of the commits should
  // be the values added up to that point and also the root node of the given
  // commit.
  for (int i = 0; i < size; ++i) {
    std::vector<ObjectId> objects;
    EXPECT_EQ(Status::OK, storage_->GetUnsyncedObjects(commits[i], &objects));
    EXPECT_EQ(static_cast<unsigned>(i + 2), objects.size());

    std::unique_ptr<const Commit> commit;
    EXPECT_EQ(Status::OK, storage_->GetCommit(commits[i], &commit));
    EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                          commit->GetRootId()) != objects.end());
    for (int j = 0; j <= i; ++j) {
      EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                            data[j].object_id) != objects.end());
    }
  }

  // Mark the 2nd object as synced. We now expect to find the 2 unsynced values
  // and the (also unsynced) root node.
  EXPECT_EQ(Status::OK, storage_->MarkObjectSynced(data[1].object_id));
  std::vector<ObjectId> objects;
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedObjects(commits[2], &objects));
  EXPECT_EQ(3u, objects.size());
  std::unique_ptr<const Commit> commit;
  EXPECT_EQ(Status::OK, storage_->GetCommit(commits[2], &commit));
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), commit->GetRootId()) !=
              objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), data[0].object_id) !=
              objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), data[2].object_id) !=
              objects.end());
}

TEST_F(PageStorageTest, UntrackedObjectsSimple) {
  ObjectData data("Some data");

  // The object is not yet created and its id should not be marked as untracked.
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));

  // After creating the object it should be marked as untracked.
  TryAddFromLocal(data.value, data.object_id);
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));

  // After adding the object in a commit it should not be untracked any more.
  std::unique_ptr<Journal> journal;
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::IMPLICIT, &journal));
  EXPECT_EQ(Status::OK,
            journal->Put("key", data.object_id, KeyPriority::EAGER));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
  journal->Commit(
      [](Status status, const CommitId& id) { EXPECT_EQ(Status::OK, status); });
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, UntrackedObjectsComplex) {
  ObjectData data[] = {
      ObjectData("Some data"), ObjectData("Some more data"),
      ObjectData("Even more data"),
  };
  for (int i = 0; i < 3; ++i) {
    TryAddFromLocal(data[i].value, data[i].object_id);
    EXPECT_TRUE(storage_->ObjectIsUntracked(data[i].object_id));
  }

  // Add a first commit containing object_ids[0].
  std::unique_ptr<Journal> journal;
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::IMPLICIT, &journal));
  EXPECT_EQ(Status::OK,
            journal->Put("key0", data[0].object_id, KeyPriority::LAZY));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data[0].object_id));
  journal->Commit(
      [](Status status, const CommitId& id) { EXPECT_EQ(Status::OK, status); });
  EXPECT_FALSE(storage_->ObjectIsUntracked(data[0].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data[1].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data[2].object_id));

  // Create a second commit. After calling Put for "key1" for the second time
  // object_ids[1] is no longer part of this commit: it should remain untracked
  // after committing.
  journal.reset();
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::IMPLICIT, &journal));
  EXPECT_EQ(Status::OK,
            journal->Put("key1", data[1].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key2", data[2].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key1", data[2].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key3", data[0].object_id, KeyPriority::LAZY));
  journal->Commit(
      [](Status status, const CommitId& id) { EXPECT_EQ(Status::OK, status); });

  EXPECT_FALSE(storage_->ObjectIsUntracked(data[0].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data[1].object_id));
  EXPECT_FALSE(storage_->ObjectIsUntracked(data[2].object_id));
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

TEST_F(PageStorageTest, OrderOfCommitWatch) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  std::unique_ptr<Journal> journal;
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::EXPLICIT, &journal));
  EXPECT_EQ(Status::OK,
            journal->Put("key1", RandomId(kObjectIdSize), KeyPriority::EAGER));

  ObjectId commit_id;
  journal->Commit(
      [this, &commit_id, &watcher](Status status, const CommitId& id) {
        EXPECT_EQ(Status::OK, status);
        commit_id = id;
        // We should get the callback before the watchers.
        EXPECT_EQ(0, watcher.commit_count);
      });

  EXPECT_EQ(1, watcher.commit_count);
  EXPECT_EQ(commit_id, watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);
}

TEST_F(PageStorageTest, SyncMetadata) {
  std::string sync_state;
  EXPECT_EQ(Status::NOT_FOUND, storage_->GetSyncMetadata(&sync_state));

  EXPECT_EQ(Status::OK, storage_->SetSyncMetadata("bazinga"));
  EXPECT_EQ(Status::OK, storage_->GetSyncMetadata(&sync_state));
  EXPECT_EQ("bazinga", sync_state);
}

}  // namespace
}  // namespace storage
