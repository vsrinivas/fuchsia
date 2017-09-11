// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_storage_impl.h"

#include <dirent.h>

#include <chrono>
#include <memory>
#include <thread>

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/callback/synchronous_task.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/btree/encoding.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/commit_random_impl.h"
#include "apps/ledger/src/storage/impl/constants.h"
#include "apps/ledger/src/storage/impl/directory_reader.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/impl/object_id.h"
#include "apps/ledger/src/storage/impl/page_db_empty_impl.h"
#include "apps/ledger/src/storage/impl/split.h"
#include "apps/ledger/src/storage/impl/storage_test_utils.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace storage {

class PageStorageImplAccessorForTest {
 public:
  static void AddPiece(const std::unique_ptr<PageStorageImpl>& storage,
                       ObjectId object_id,
                       std::unique_ptr<DataSource::DataChunk> chunk,
                       ChangeSource source,
                       std::function<void(Status)> callback) {
    storage->AddPiece(std::move(object_id), std::move(chunk), source,
                      std::move(callback));
  }

  static PageDb& GetDb(const std::unique_ptr<PageStorageImpl>& storage) {
    return storage->db_;
  }
};

namespace {

using coroutine::CoroutineHandler;

std::vector<PageStorage::CommitIdAndBytes> CommitAndBytesFromCommit(
    const Commit& commit) {
  std::vector<PageStorage::CommitIdAndBytes> result;
  result.emplace_back(commit.GetId(), commit.GetStorageBytes().ToString());
  return result;
}

class FakeCommitWatcher : public CommitWatcher {
 public:
  FakeCommitWatcher() {}

  void OnNewCommits(const std::vector<std::unique_ptr<const Commit>>& commits,
                    ChangeSource source) override {
    ++commit_count;
    last_commit_id = commits.back()->GetId();
    last_source = source;
  }

  int commit_count = 0;
  CommitId last_commit_id;
  ChangeSource last_source;
};

class DelayingFakeSyncDelegate : public PageSyncDelegate {
 public:
  explicit DelayingFakeSyncDelegate(
      std::function<void(ftl::Closure)> on_get_object)
      : on_get_object_(std::move(on_get_object)) {}

  void AddObject(ObjectIdView object_id, const std::string& value) {
    id_to_value_[object_id.ToString()] = value;
  }

  void GetObject(
      ObjectIdView object_id,
      std::function<void(Status status, uint64_t size, mx::socket data)>
          callback) override {
    std::string id = object_id.ToString();
    std::string& value = id_to_value_[id];
    object_requests.insert(id);
    on_get_object_([ callback = std::move(callback), value ] {
      callback(Status::OK, value.size(), mtl::WriteStringToSocket(value));
    });
  }

  std::set<ObjectId> object_requests;

 private:
  std::function<void(ftl::Closure)> on_get_object_;
  std::map<ObjectId, std::string> id_to_value_;
};

class FakeSyncDelegate : public DelayingFakeSyncDelegate {
 public:
  FakeSyncDelegate()
      : DelayingFakeSyncDelegate([](ftl::Closure callback) { callback(); }) {}
};

// Implements |Init()|, |CreateJournal() and |CreateMergeJournal()| and
// fails with a |NOT_IMPLEMENTED| error in all other cases.
class FakePageDbImpl : public PageDbEmptyImpl {
 public:
  FakePageDbImpl(coroutine::CoroutineService* coroutine_service,
                 PageStorageImpl* page_storage)
      : coroutine_service_(coroutine_service), storage_(page_storage) {}

  Status Init() override { return Status::OK; }
  Status CreateJournal(CoroutineHandler* /*handler*/,
                       JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override {
    JournalId id = RandomString(10);
    *journal = JournalDBImpl::Simple(journal_type, coroutine_service_, storage_,
                                     this, id, base);
    return Status::OK;
  }

  Status CreateMergeJournal(CoroutineHandler* /*handler*/,
                            const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override {
    *journal = JournalDBImpl::Merge(coroutine_service_, storage_, this,
                                    RandomString(10), base, other);
    return Status::OK;
  }

  std::unique_ptr<PageDb::Batch> StartBatch() override {
    return std::make_unique<FakePageDbImpl>(coroutine_service_, storage_);
  }

 private:
  coroutine::CoroutineService* coroutine_service_;
  PageStorageImpl* storage_;
};

class PageStorageTest : public StorageTest {
 public:
  PageStorageTest() {}

  ~PageStorageTest() override {}

  // Test:
  void SetUp() override {
    ::test::TestWithMessageLoop::SetUp();

    PageId id = RandomString(10);
    storage_ = std::make_unique<PageStorageImpl>(&coroutine_service_,
                                                 tmp_dir_.path(), id);

    Status status;
    storage_->Init(callback::Capture(MakeQuitTask(), &status));
    message_loop_.Run();
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  PageStorage* GetStorage() override { return storage_.get(); }

  std::vector<CommitId> GetHeads() {
    Status status;
    std::vector<CommitId> ids;
    storage_->GetHeadCommitIds(
        callback::Capture(MakeQuitTask(), &status, &ids));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    return ids;
  }

  std::unique_ptr<const Commit> GetFirstHead() {
    std::vector<CommitId> ids = GetHeads();
    EXPECT_FALSE(ids.empty());
    return GetCommit(ids[0]);
  }

  std::unique_ptr<const Commit> GetCommit(const CommitId& id) {
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->GetCommit(id,
                        callback::Capture(MakeQuitTask(), &status, &commit));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    return commit;
  }

  CommitId TryCommitFromSync() {
    ObjectId root_id;
    EXPECT_TRUE(GetEmptyNodeId(&root_id));

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), root_id, std::move(parent));
    CommitId id = commit->GetId();

    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit),
                                 [this](Status status) {
                                   EXPECT_EQ(Status::OK, status);
                                   message_loop_.PostQuitTask();
                                 });
    EXPECT_FALSE(RunLoopWithTimeout());
    return id;
  }

  std::unique_ptr<const Commit> TryCommitJournal(
      std::unique_ptr<Journal> journal,
      Status expected_status) {
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->CommitJournal(
        std::move(journal),
        callback::Capture(MakeQuitTask(), &status, &commit));

    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(expected_status, status);
    return commit;
  }

  CommitId TryCommitFromLocal(JournalType type,
                              int keys,
                              size_t min_key_size = 0) {
    Status status;
    std::unique_ptr<Journal> journal;
    storage_->StartCommit(GetFirstHead()->GetId(), type,
                          callback::Capture(MakeQuitTask(), &status, &journal));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    EXPECT_NE(nullptr, journal);

    for (int i = 0; i < keys; ++i) {
      auto key = ftl::StringPrintf("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      EXPECT_EQ(Status::OK,
                journal->Put(key, RandomObjectId(), KeyPriority::EAGER));
    }
    EXPECT_EQ(Status::OK, journal->Delete("key_does_not_exist"));

    std::unique_ptr<const Commit> commit =
        TryCommitJournal(std::move(journal), Status::OK);

    // Check the contents.
    std::vector<Entry> entries = GetCommitContents(*commit);
    EXPECT_EQ(static_cast<size_t>(keys), entries.size());
    for (int i = 0; i < keys; ++i) {
      auto key = ftl::StringPrintf("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      EXPECT_EQ(key, entries[i].key);
    }

    return commit->GetId();
  }

  void TryAddFromLocal(std::string content, const ObjectId& expected_id) {
    storage_->AddObjectFromLocal(
        DataSource::Create(std::move(content)),
        [this, &expected_id](Status returned_status, ObjectId object_id) {
          EXPECT_EQ(Status::OK, returned_status);
          EXPECT_EQ(expected_id, object_id);
          message_loop_.PostQuitTask();
        });
    EXPECT_FALSE(RunLoopWithTimeout());
  }

  std::unique_ptr<const Object> TryGetObject(
      const ObjectId& object_id,
      PageStorage::Location location,
      Status expected_status = Status::OK) {
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetObject(object_id, location,
                        callback::Capture(MakeQuitTask(), &status, &object));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(expected_status, status);
    return object;
  }

  std::unique_ptr<const Object> TryGetPiece(
      const ObjectId& object_id,
      Status expected_status = Status::OK) {
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetPiece(object_id,
                       callback::Capture(MakeQuitTask(), &status, &object));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(expected_status, status);
    return object;
  }

  std::vector<Entry> GetCommitContents(const Commit& commit) {
    Status status;
    std::vector<Entry> result;
    auto on_next = [&result](Entry e) {
      result.push_back(e);
      return true;
    };
    storage_->GetCommitContents(commit, "", std::move(on_next),
                                callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());

    EXPECT_EQ(Status::OK, status);
    return result;
  }

  std::vector<std::unique_ptr<const Commit>> GetUnsyncedCommits() {
    Status status;
    std::vector<std::unique_ptr<const Commit>> commits;
    storage_->GetUnsyncedCommits(
        callback::Capture(MakeQuitTask(), &status, &commits));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    return commits;
  }

  Status WriteObject(
      CoroutineHandler* handler,
      ObjectData* data,
      PageDbObjectStatus object_status = PageDbObjectStatus::TRANSIENT) {
    return PageStorageImplAccessorForTest::GetDb(storage_).WriteObject(
        handler, data->object_id, data->ToChunk(), object_status);
  }

  Status ReadObject(ObjectId object_id, std::unique_ptr<const Object>* object) {
    return PageStorageImplAccessorForTest::GetDb(storage_).ReadObject(object_id,
                                                                      object);
  }

  Status DeleteObject(CoroutineHandler* handler, ObjectId object_id) {
    return PageStorageImplAccessorForTest::GetDb(storage_).DeleteObject(
        handler, object_id);
  }

  coroutine::CoroutineServiceImpl coroutine_service_;
  std::thread io_thread_;
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<PageStorageImpl> storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorageTest);
};

TEST_F(PageStorageTest, AddGetLocalCommits) {
  // Search for a commit id that doesn't exist and see the error.
  Status status;
  std::unique_ptr<const Commit> lookup_commit;
  storage_->GetCommit(
      RandomCommitId(),
      callback::Capture(MakeQuitTask(), &status, &lookup_commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_FALSE(lookup_commit);

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  // Search for a commit that exist and check the content.
  storage_->AddCommitFromLocal(std::move(commit), {}, [](Status status) {
    EXPECT_EQ(Status::OK, status);
  });
  std::unique_ptr<const Commit> found = GetCommit(id);
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddCommitFromLocalDoNotMarkUnsynedAlreadySyncedCommit) {
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  storage_->AddCommitFromLocal(commit->Clone(), {}, [](Status status) {
    EXPECT_EQ(Status::OK, status);
  });

  auto commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(id, commits[0]->GetId());

  Status status;
  storage_->MarkCommitSynced(id, callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  // Add the commit again.
  storage_->AddCommitFromLocal(commit->Clone(), {}, [](Status status) {
    EXPECT_EQ(Status::OK, status);
  });

  // Check that the commit is not marked unsynced.
  commits = GetUnsyncedCommits();
  EXPECT_EQ(0u, commits.size());
}

TEST_F(PageStorageTest, AddCommitBeforeParentsError) {
  // Try to add a commit before its parent and see the error.
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(new test::CommitRandomImpl());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));

  storage_->AddCommitFromLocal(std::move(commit), {}, [](Status status) {
    EXPECT_EQ(Status::ILLEGAL_STATE, status);
  });
}

TEST_F(PageStorageTest, AddCommitsOutOfOrder) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, std::vector<ObjectId>(1), &node));
  ObjectId root_id = node->GetId();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit1 = CommitImpl::FromContentAndParents(storage_.get(), root_id,
                                                   std::move(parent));
  parent.clear();
  parent.push_back(commit1->Clone());
  auto commit2 = CommitImpl::FromContentAndParents(storage_.get(), root_id,
                                                   std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit2->GetId(),
                                 commit2->GetStorageBytes().ToString());
  commits_and_bytes.emplace_back(commit1->GetId(),
                                 commit1->GetStorageBytes().ToString());

  Status status;
  storage_->AddCommitsFromSync(std::move(commits_and_bytes),
                               callback::Capture(MakeQuitTask(), &status));
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageStorageTest, AddGetSyncedCommits) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    FakeSyncDelegate sync;
    storage_->SetSyncDelegate(&sync);

    // Create a node with 2 values.
    ObjectData lazy_value("Some data", InlineBehavior::PREVENT);
    ObjectData eager_value("More data", InlineBehavior::PREVENT);
    std::vector<Entry> entries = {
        Entry{"key0", lazy_value.object_id, KeyPriority::LAZY},
        Entry{"key1", eager_value.object_id, KeyPriority::EAGER},
    };
    std::unique_ptr<const btree::TreeNode> node;
    ASSERT_TRUE(CreateNodeFromEntries(
        entries, std::vector<ObjectId>(entries.size() + 1), &node));
    ObjectId root_id = node->GetId();

    // Add the three objects to FakeSyncDelegate.
    sync.AddObject(lazy_value.object_id, lazy_value.value);
    sync.AddObject(eager_value.object_id, eager_value.value);
    std::unique_ptr<const Object> root_object =
        TryGetObject(root_id, PageStorage::Location::NETWORK);

    ftl::StringView root_data;
    ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
    sync.AddObject(root_id, root_data.ToString());

    // Remove the root from the local storage. The two values were never added.
    ASSERT_EQ(Status::OK, DeleteObject(handler, root_id));

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), root_id, std::move(parent));
    CommitId id = commit->GetId();

    // Adding the commit should only request the tree node and the eager value.
    sync.object_requests.clear();
    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit),
                                 [this](Status status) {
                                   EXPECT_EQ(Status::OK, status);
                                   message_loop_.PostQuitTask();
                                 });
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(2u, sync.object_requests.size());
    EXPECT_TRUE(sync.object_requests.find(root_id) !=
                sync.object_requests.end());
    EXPECT_TRUE(sync.object_requests.find(eager_value.object_id) !=
                sync.object_requests.end());

    // Adding the same commit twice should not request any objects from sync.
    sync.object_requests.clear();
    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit),
                                 [this](Status status) {
                                   EXPECT_EQ(Status::OK, status);
                                   message_loop_.PostQuitTask();
                                 });
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_TRUE(sync.object_requests.empty());

    std::unique_ptr<const Commit> found = GetCommit(id);
    EXPECT_EQ(commit->GetStorageBytes(), found->GetStorageBytes());

    // Check that the commit is not marked as unsynced.
    std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();
    EXPECT_TRUE(commits.empty());
  }));
}

// Check that receiving a remote commit that is already present locally but not
// synced will mark the commit as synced.
TEST_F(PageStorageTest, MarkRemoteCommitSynced) {
  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);

  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, std::vector<ObjectId>(1), &node));
  ObjectId root_id = node->GetId();

  std::unique_ptr<const Object> root_object =
      TryGetObject(root_id, PageStorage::Location::NETWORK);

  ftl::StringView root_data;
  ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
  sync.AddObject(root_id, root_data.ToString());

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), root_id, std::move(parent));
  CommitId id = commit->GetId();

  Status status;
  storage_->AddCommitFromLocal(std::move(commit), {},
                               callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(1u, GetUnsyncedCommits().size());
  storage_->GetCommit(id, callback::Capture(MakeQuitTask(), &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(),
                                 commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(std::move(commits_and_bytes),
                               callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(0u, GetUnsyncedCommits().size());
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();

  // Initially there should be no unsynced commits.
  EXPECT_TRUE(commits.empty());

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  // After adding a commit it should marked as unsynced.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  storage_->AddCommitFromLocal(std::move(commit), {}, [](Status status) {
    EXPECT_EQ(Status::OK, status);
  });
  commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(storage_bytes, commits[0]->GetStorageBytes());

  // Mark it as synced.
  Status status;
  storage_->MarkCommitSynced(id, callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  commits = GetUnsyncedCommits();
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, HeadCommits) {
  // Every page should have one initial head commit.
  std::vector<CommitId> heads = GetHeads();
  EXPECT_EQ(1u, heads.size());

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  // Adding a new commit with the previous head as its parent should replace the
  // old head.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id = commit->GetId();

  storage_->AddCommitFromLocal(std::move(commit), {}, [](Status status) {
    EXPECT_EQ(Status::OK, status);
  });
  heads = GetHeads();
  ASSERT_EQ(1u, heads.size());
  EXPECT_EQ(id, heads[0]);
}

TEST_F(PageStorageTest, CreateJournals) {
  // Explicit journal.
  CommitId left_id = TryCommitFromLocal(JournalType::EXPLICIT, 5);
  CommitId right_id = TryCommitFromLocal(JournalType::IMPLICIT, 10);

  // Journal for merge commit.
  storage::Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartMergeCommit(
      left_id, right_id, callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_NE(nullptr, journal);
  EXPECT_EQ(Status::OK, storage_->RollbackJournal(std::move(journal)));
}

TEST_F(PageStorageTest, CreateJournalHugeNode) {
  CommitId id = TryCommitFromLocal(JournalType::EXPLICIT, 500, 1024);
  std::unique_ptr<const Commit> commit = GetCommit(id);
  std::vector<Entry> entries = GetCommitContents(*commit);

  EXPECT_EQ(500u, entries.size());
  for (const auto& entry : entries) {
    EXPECT_EQ(1024u, entry.key.size());
  }

  // Check that all node's parts are marked as unsynced.
  Status status;
  std::vector<ObjectId> object_ids;
  storage_->GetUnsyncedPieces(
      callback::Capture(MakeQuitTask(), &status, &object_ids));
  EXPECT_FALSE(RunLoopWithTimeout());

  bool found_index = false;
  std::unordered_set<ObjectId> unsynced_ids(object_ids.begin(),
                                            object_ids.end());
  for (const auto& id : unsynced_ids) {
    EXPECT_FALSE(GetObjectIdType(id) == ObjectIdType::INLINE);

    if (GetObjectIdType(id) == ObjectIdType::INDEX_HASH) {
      found_index = true;
      std::unordered_set<ObjectId> sub_ids;
      IterationStatus iteration_status = IterationStatus::ERROR;
      CollectPieces(
          id,
          [this](ObjectIdView id,
                 std::function<void(Status, ftl::StringView)> callback) {
            storage_->GetPiece(
                id, [callback = std::move(callback)](
                        Status status, std::unique_ptr<const Object> object) {
                  if (status != Status::OK) {
                    callback(status, "");
                    return;
                  }
                  ftl::StringView data;
                  status = object->GetData(&data);
                  callback(status, data);
                });
          },
          [this, &iteration_status, &sub_ids](IterationStatus status,
                                              ObjectIdView id) {
            iteration_status = status;
            if (status == IterationStatus::IN_PROGRESS) {
              EXPECT_TRUE(sub_ids.insert(id.ToString()).second);
            } else {
              message_loop_.PostQuitTask();
            }
            return true;
          });
      EXPECT_EQ(IterationStatus::DONE, iteration_status);
      for (const auto& id : sub_ids) {
        EXPECT_EQ(1u, unsynced_ids.count(id));
      }
    }
  }
  EXPECT_TRUE(found_index);
}

TEST_F(PageStorageTest, JournalCommitFailsAfterFailedOperation) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    FakePageDbImpl db(&coroutine_service_, storage_.get());

    std::unique_ptr<Journal> journal;
    // Explicit journals.
    // The first call will fail because FakePageDbImpl::AddJournalEntry()
    // returns an error. After a failed call all other Put/Delete/Commit
    // operations should fail with ILLEGAL_STATE.
    db.CreateJournal(handler, JournalType::EXPLICIT, RandomCommitId(),
                     &journal);
    EXPECT_NE(Status::OK, journal->Put("key", "value", KeyPriority::EAGER));
    EXPECT_EQ(Status::ILLEGAL_STATE,
              journal->Put("key", "value", KeyPriority::EAGER));
    EXPECT_EQ(Status::ILLEGAL_STATE, journal->Delete("key"));

    TryCommitJournal(std::move(journal), Status::ILLEGAL_STATE);

    // Implicit journals.
    // All calls will fail because of FakePageDbImpl implementation, not because
    // of an ILLEGAL_STATE error.
    db.CreateJournal(handler, JournalType::IMPLICIT, RandomCommitId(),
                     &journal);
    EXPECT_NE(Status::OK, journal->Put("key", "value", KeyPriority::EAGER));
    Status put_status = journal->Put("key", "value", KeyPriority::EAGER);
    EXPECT_NE(Status::ILLEGAL_STATE, put_status);
    EXPECT_NE(Status::ILLEGAL_STATE, journal->Delete("key"));
    storage_->CommitJournal(std::move(journal),
                            [this](Status s, std::unique_ptr<const Commit>) {
                              EXPECT_NE(Status::ILLEGAL_STATE, s);
                              message_loop_.PostQuitTask();
                            });
    ASSERT_FALSE(RunLoopWithTimeout());
  }));
}

TEST_F(PageStorageTest, DestroyUncommittedJournal) {
  // It is not an error if a journal is not committed or rolled back.
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(GetFirstHead()->GetId(), JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, journal);
  EXPECT_EQ(Status::OK,
            journal->Put("key", RandomObjectId(), KeyPriority::EAGER));
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  ObjectData data("Some data", InlineBehavior::PREVENT);

  ObjectId object_id;
  storage_->AddObjectFromLocal(
      data.ToDataSource(),
      [this, &object_id](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        object_id = std::move(returned_object_id);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(data.object_id, object_id);

  std::unique_ptr<const Object> object;
  ASSERT_EQ(Status::OK, ReadObject(object_id, &object));
  ftl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_TRUE(storage_->ObjectIsUntracked(object_id));
}

TEST_F(PageStorageTest, AddSmallObjectFromLocal) {
  ObjectData data("Some data");

  ObjectId object_id;
  storage_->AddObjectFromLocal(
      data.ToDataSource(),
      [this, &object_id](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::OK, returned_status);
        object_id = std::move(returned_object_id);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(data.object_id, object_id);
  EXPECT_EQ(data.value, object_id);

  std::unique_ptr<const Object> object;
  EXPECT_EQ(Status::NOT_FOUND, ReadObject(object_id, &object));
  // Inline object do not need to ever be tracked.
  EXPECT_FALSE(storage_->ObjectIsUntracked(object_id));
}

TEST_F(PageStorageTest, InterruptAddObjectFromLocal) {
  ObjectData data("Some data");

  ObjectId object_id;
  storage_->AddObjectFromLocal(
      data.ToDataSource(),
      [](Status returned_status, ObjectId returned_object_id) {});

  // Checking that we do not crash when deleting the storage while an AddObject
  // call is in progress.
  storage_.reset();
}

TEST_F(PageStorageTest, AddObjectFromLocalWrongSize) {
  ObjectData data("Some data");

  storage_->AddObjectFromLocal(
      DataSource::Create(mtl::WriteStringToSocket(data.value), 123),
      [this](Status returned_status, ObjectId returned_object_id) {
        EXPECT_EQ(Status::IO_ERROR, returned_status);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, AddLocalPiece) {
  ObjectData data("Some data", InlineBehavior::PREVENT);

  PageStorageImplAccessorForTest::AddPiece(
      storage_, data.object_id, data.ToChunk(), ChangeSource::LOCAL,
      [this](Status returned_status) {
        EXPECT_EQ(Status::OK, returned_status);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  std::unique_ptr<const Object> object;
  ASSERT_EQ(Status::OK, ReadObject(data.object_id, &object));
  ftl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, AddSyncPiece) {
  ObjectData data("Some data", InlineBehavior::PREVENT);

  PageStorageImplAccessorForTest::AddPiece(
      storage_, data.object_id, data.ToChunk(), ChangeSource::SYNC,
      [this](Status returned_status) {
        EXPECT_EQ(Status::OK, returned_status);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());

  std::unique_ptr<const Object> object;
  ASSERT_EQ(Status::OK, ReadObject(data.object_id, &object));
  ftl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, GetObject) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    ObjectData data("Some data");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    std::unique_ptr<const Object> object =
        TryGetObject(data.object_id, PageStorage::Location::LOCAL);
    EXPECT_EQ(data.object_id, object->GetId());
    ftl::StringView object_data;
    ASSERT_EQ(Status::OK, object->GetData(&object_data));
    EXPECT_EQ(data.value, convert::ToString(object_data));
  }));
}

TEST_F(PageStorageTest, GetObjectFromSync) {
  ObjectData data("Some data", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_id, data.value);
  storage_->SetSyncDelegate(&sync);

  std::unique_ptr<const Object> object =
      TryGetObject(data.object_id, PageStorage::Location::NETWORK);
  EXPECT_EQ(data.object_id, object->GetId());
  ftl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data.value, convert::ToString(object_data));

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data("Some other data", InlineBehavior::PREVENT);
  TryGetObject(other_data.object_id, PageStorage::Location::LOCAL,
               Status::NOT_FOUND);
  TryGetObject(other_data.object_id, PageStorage::Location::NETWORK,
               Status::NOT_CONNECTED_ERROR);
}

TEST_F(PageStorageTest, GetObjectFromSyncWrongId) {
  ObjectData data("Some data", InlineBehavior::PREVENT);
  ObjectData data2("Some data2", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_id, data2.value);
  storage_->SetSyncDelegate(&sync);

  TryGetObject(data.object_id, PageStorage::Location::NETWORK,
               Status::OBJECT_ID_MISMATCH);
}

TEST_F(PageStorageTest, AddAndGetHugeObjectFromLocal) {
  std::string data_str = RandomString(65536);

  ObjectData data(std::move(data_str), InlineBehavior::PREVENT);

  ASSERT_EQ(ObjectIdType::INDEX_HASH, GetObjectIdType(data.object_id));

  Status status;
  ObjectId object_id;
  storage_->AddObjectFromLocal(
      data.ToDataSource(),
      callback::Capture(MakeQuitTask(), &status, &object_id));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_id, object_id);

  std::unique_ptr<const Object> object =
      TryGetObject(object_id, PageStorage::Location::LOCAL);
  ftl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_TRUE(storage_->ObjectIsUntracked(object_id));

  // Check that the object is encoded with an index, and is different than the
  // piece obtained at |object_id|.
  std::unique_ptr<const Object> piece = TryGetPiece(object_id);
  ftl::StringView piece_content;
  ASSERT_EQ(Status::OK, piece->GetData(&piece_content));
  EXPECT_NE(content, piece_content);
}

TEST_F(PageStorageTest, UnsyncedPieces) {
  ObjectData data_array[] = {
      ObjectData("Some data", InlineBehavior::PREVENT),
      ObjectData("Some more data", InlineBehavior::PREVENT),
      ObjectData("Even more data", InlineBehavior::PREVENT),
  };
  constexpr size_t size = arraysize(data_array);
  for (auto& data : data_array) {
    TryAddFromLocal(data.value, data.object_id);
    EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
  }

  std::vector<CommitId> commits;

  // Add one key-value pair per commit.
  for (size_t i = 0; i < size; ++i) {
    Status status;
    std::unique_ptr<Journal> journal;
    storage_->StartCommit(GetFirstHead()->GetId(), JournalType::IMPLICIT,
                          callback::Capture(MakeQuitTask(), &status, &journal));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);

    EXPECT_EQ(Status::OK,
              journal->Put(ftl::StringPrintf("key%lu", i),
                           data_array[i].object_id, KeyPriority::LAZY));
    TryCommitJournal(std::move(journal), Status::OK);
    commits.push_back(GetFirstHead()->GetId());
  }

  // GetUnsyncedPieces should return the ids of all objects: 3 values and
  // the 3 root nodes of the 3 commits.
  Status status;
  std::vector<ObjectId> object_ids;
  storage_->GetUnsyncedPieces(
      callback::Capture(MakeQuitTask(), &status, &object_ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(6u, object_ids.size());
  for (size_t i = 0; i < size; ++i) {
    std::unique_ptr<const Commit> commit = GetCommit(commits[i]);
    EXPECT_TRUE(std::find(object_ids.begin(), object_ids.end(),
                          commit->GetRootId()) != object_ids.end());
  }
  for (auto& data : data_array) {
    EXPECT_TRUE(std::find(object_ids.begin(), object_ids.end(),
                          data.object_id) != object_ids.end());
  }

  // Mark the 2nd object as synced. We now expect to still find the 2 unsynced
  // values and the (also unsynced) root node.
  storage_->MarkPieceSynced(data_array[1].object_id,
                            callback::Capture(MakeQuitTask(), &status));
  EXPECT_EQ(Status::OK, status);
  std::vector<ObjectId> objects;
  storage_->GetUnsyncedPieces(
      callback::Capture(MakeQuitTask(), &status, &objects));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(5u, objects.size());
  std::unique_ptr<const Commit> commit = GetCommit(commits[2]);
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), commit->GetRootId()) !=
              objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                        data_array[0].object_id) != objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                        data_array[2].object_id) != objects.end());
}

TEST_F(PageStorageTest, UntrackedObjectsSimple) {
  ObjectData data("Some data", InlineBehavior::PREVENT);

  // The object is not yet created and its id should not be marked as untracked.
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));

  // After creating the object it should be marked as untracked.
  TryAddFromLocal(data.value, data.object_id);
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));

  // After adding the object in a commit it should not be untracked any more.
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(GetFirstHead()->GetId(), JournalType::IMPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal->Put("key", data.object_id, KeyPriority::EAGER));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
  TryCommitJournal(std::move(journal), Status::OK);
  EXPECT_FALSE(storage_->ObjectIsUntracked(data.object_id));
}

TEST_F(PageStorageTest, UntrackedObjectsComplex) {
  ObjectData data_array[] = {
      ObjectData("Some data", InlineBehavior::PREVENT),
      ObjectData("Some more data", InlineBehavior::PREVENT),
      ObjectData("Even more data", InlineBehavior::PREVENT),
  };
  for (auto& data : data_array) {
    TryAddFromLocal(data.value, data.object_id);
    EXPECT_TRUE(storage_->ObjectIsUntracked(data.object_id));
  }

  // Add a first commit containing object_ids[0].
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(GetFirstHead()->GetId(), JournalType::IMPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal->Put("key0", data_array[0].object_id, KeyPriority::LAZY));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data_array[0].object_id));
  TryCommitJournal(std::move(journal), Status::OK);
  EXPECT_FALSE(storage_->ObjectIsUntracked(data_array[0].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data_array[1].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data_array[2].object_id));

  // Create a second commit. After calling Put for "key1" for the second time
  // object_ids[1] is no longer part of this commit: it should remain untracked
  // after committing.
  journal.reset();
  storage_->StartCommit(GetFirstHead()->GetId(), JournalType::IMPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal->Put("key1", data_array[1].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key2", data_array[2].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key1", data_array[2].object_id, KeyPriority::LAZY));
  EXPECT_EQ(Status::OK,
            journal->Put("key3", data_array[0].object_id, KeyPriority::LAZY));
  TryCommitJournal(std::move(journal), Status::OK);

  EXPECT_FALSE(storage_->ObjectIsUntracked(data_array[0].object_id));
  EXPECT_TRUE(storage_->ObjectIsUntracked(data_array[1].object_id));
  EXPECT_FALSE(storage_->ObjectIsUntracked(data_array[2].object_id));
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

  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(GetFirstHead()->GetId(), JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal->Put("key1", RandomObjectId(), KeyPriority::EAGER));

  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(
                              [this, &watcher] {
                                // We should get the callback before the
                                // watchers.
                                EXPECT_EQ(0, watcher.commit_count);
                                message_loop_.PostQuitTask();
                              },
                              &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(1, watcher.commit_count);
  EXPECT_EQ(commit->GetId(), watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);
}

TEST_F(PageStorageTest, SyncMetadata) {
  std::vector<std::pair<ftl::StringView, ftl::StringView>> keys_and_values = {
      {"foo1", "foo2"}, {"bar1", " bar2 "}};
  for (auto key_and_value : keys_and_values) {
    auto key = key_and_value.first;
    auto value = key_and_value.second;
    std::string returned_value;
    EXPECT_EQ(Status::NOT_FOUND,
              storage_->GetSyncMetadata(key, &returned_value));

    Status status;
    storage_->SetSyncMetadata(key, value,
                              callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(Status::OK, storage_->GetSyncMetadata(key, &returned_value));
    EXPECT_EQ(value, returned_value);
  }
}

TEST_F(PageStorageTest, AddMultipleCommitsFromSync) {
  EXPECT_TRUE(RunInCoroutine([&](CoroutineHandler* handler) {
    FakeSyncDelegate sync;
    storage_->SetSyncDelegate(&sync);

    // Build the commit Tree with:
    //         0
    //         |
    //         1  2
    std::vector<ObjectId> object_ids;
    object_ids.resize(3);
    for (size_t i = 0; i < object_ids.size(); ++i) {
      ObjectData value("value" + std::to_string(i), InlineBehavior::PREVENT);
      std::vector<Entry> entries = {Entry{"key" + std::to_string(i),
                                          value.object_id, KeyPriority::EAGER}};
      std::unique_ptr<const btree::TreeNode> node;
      ASSERT_TRUE(CreateNodeFromEntries(
          entries, std::vector<ObjectId>(entries.size() + 1), &node));
      object_ids[i] = node->GetId();
      sync.AddObject(value.object_id, value.value);
      std::unique_ptr<const Object> root_object =
          TryGetObject(object_ids[i], PageStorage::Location::NETWORK);
      ftl::StringView root_data;
      ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
      sync.AddObject(object_ids[i], root_data.ToString());

      // Remove the root from the local storage. The value was never added.
      ASSERT_EQ(Status::OK, DeleteObject(handler, object_ids[i]));
    }

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit0 = CommitImpl::FromContentAndParents(
        storage_.get(), object_ids[0], std::move(parent));
    parent.clear();

    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit1 = CommitImpl::FromContentAndParents(
        storage_.get(), object_ids[1], std::move(parent));
    parent.clear();

    parent.emplace_back(commit1->Clone());
    std::unique_ptr<const Commit> commit2 = CommitImpl::FromContentAndParents(
        storage_.get(), object_ids[2], std::move(parent));

    std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
    commits_and_bytes.emplace_back(commit0->GetId(),
                                   commit0->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit1->GetId(),
                                   commit1->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit2->GetId(),
                                   commit2->GetStorageBytes().ToString());

    Status status;
    storage_->AddCommitsFromSync(std::move(commits_and_bytes),
                                 callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);

    EXPECT_EQ(4u, sync.object_requests.size());
    EXPECT_NE(sync.object_requests.find(object_ids[0]),
              sync.object_requests.end());
    EXPECT_EQ(sync.object_requests.find(object_ids[1]),
              sync.object_requests.end());
    EXPECT_NE(sync.object_requests.find(object_ids[2]),
              sync.object_requests.end());
  }));
}

TEST_F(PageStorageTest, Generation) {
  const CommitId commit_id1 = TryCommitFromLocal(JournalType::EXPLICIT, 3);
  std::unique_ptr<const Commit> commit1 = GetCommit(commit_id1);
  EXPECT_EQ(1u, commit1->GetGeneration());

  const CommitId commit_id2 = TryCommitFromLocal(JournalType::EXPLICIT, 3);
  std::unique_ptr<const Commit> commit2 = GetCommit(commit_id2);
  EXPECT_EQ(2u, commit2->GetGeneration());

  storage::Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartMergeCommit(
      commit_id1, commit_id2,
      callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_EQ(Status::OK, status);

  std::unique_ptr<const Commit> commit3 =
      TryCommitJournal(std::move(journal), Status::OK);
  EXPECT_EQ(3u, commit3->GetGeneration());
}

TEST_F(PageStorageTest, DeletionOnIOThread) {
  std::thread io_thread;
  ftl::RefPtr<ftl::TaskRunner> io_runner;
  io_thread = mtl::CreateThread(&io_runner);
  io_runner->PostTask([] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  bool called = false;
  EXPECT_FALSE(callback::RunSynchronously(
      io_runner, [&called] { called = true; }, ftl::TimeDelta::FromSeconds(1)));
  EXPECT_FALSE(called);
  io_thread.join();
}

TEST_F(PageStorageTest, GetEntryFromCommit) {
  int size = 10;
  CommitId commit_id = TryCommitFromLocal(JournalType::EXPLICIT, size);
  std::unique_ptr<const Commit> commit = GetCommit(commit_id);

  Status status;
  Entry entry;
  storage_->GetEntryFromCommit(
      *commit, "key not found",
      callback::Capture(MakeQuitTask(), &status, &entry));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::NOT_FOUND, status);

  for (int i = 0; i < size; ++i) {
    std::string expected_key = ftl::StringPrintf("key%05d", i);
    storage_->GetEntryFromCommit(
        *commit, expected_key,
        callback::Capture(MakeQuitTask(), &status, &entry));
    ASSERT_FALSE(RunLoopWithTimeout());
    ASSERT_EQ(Status::OK, status);
    EXPECT_EQ(expected_key, entry.key);
  }
}

TEST_F(PageStorageTest, WatcherForReEntrantCommits) {
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());

  std::unique_ptr<Commit> commit1 = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id1 = commit1->GetId();

  parent.clear();
  parent.emplace_back(commit1->Clone());

  std::unique_ptr<Commit> commit2 = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectId(), std::move(parent));
  CommitId id2 = commit2->GetId();

  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  storage_->AddCommitFromLocal(
      std::move(commit1), {},
      ftl::MakeCopyable([ this, commit2 = std::move(commit2) ](
          Status status) mutable {
        EXPECT_EQ(Status::OK, status);
        storage_->AddCommitFromLocal(std::move(commit2), {}, [](Status status) {
          EXPECT_EQ(Status::OK, status);
        });
      }));

  EXPECT_EQ(2, watcher.commit_count);
  EXPECT_EQ(id2, watcher.last_commit_id);
}

TEST_F(PageStorageTest, NoOpCommit) {
  std::vector<CommitId> heads = GetHeads();
  ASSERT_FALSE(heads.empty());

  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(heads[0], JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  // Create a key, and delete it.
  EXPECT_EQ(Status::OK,
            journal->Put("key", RandomObjectId(), KeyPriority::EAGER));
  EXPECT_EQ(Status::OK, journal->Delete("key"));

  // Commit the journal.
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(MakeQuitTask(), &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(Status::OK, status);
  ASSERT_TRUE(commit);
  // Expect that the commit id is the same as the original one.
  EXPECT_EQ(heads[0], commit->GetId());
}

// Check that receiving a remote commit and commiting locally at the same time
// do not prevent the commit to be marked as unsynced.
TEST_F(PageStorageTest, MarkRemoteCommitSyncedRace) {
  ftl::Closure sync_delegate_call;
  DelayingFakeSyncDelegate sync(
      callback::Capture(MakeQuitTask(), &sync_delegate_call));
  storage_->SetSyncDelegate(&sync);

  // We need to create new nodes for the storage to be asynchronous. The empty
  // node is already there, so we create two (child, which is empty, and root,
  // which contains child).
  std::string child_data =
      btree::EncodeNode(0u, std::vector<Entry>(), std::vector<ObjectId>(1));
  ObjectId child_id = ComputeObjectId(ObjectType::VALUE, child_data);
  sync.AddObject(child_id, child_data);

  std::string root_data = btree::EncodeNode(0u, std::vector<Entry>(),
                                            std::vector<ObjectId>{child_id});
  ObjectId root_id = ComputeObjectId(ObjectType::VALUE, root_data);
  sync.AddObject(root_id, root_data);

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());

  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), root_id, std::move(parent));
  CommitId id = commit->GetId();

  // Start adding the remote commit.
  Status status;
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(),
                                 commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(std::move(commits_and_bytes),
                               callback::Capture(MakeQuitTask(), &status));
  // Make the run loop stop before the process finishes.
  EXPECT_FALSE(RunLoopWithTimeout());

  // Add the local commit.
  storage_->AddCommitFromLocal(std::move(commit), {},
                               callback::Capture(MakeQuitTask(), &status));
  // Let the two add commit finish.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(sync_delegate_call);
  sync_delegate_call();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  // Verify that the commit is added correctly.
  storage_->GetCommit(id, callback::Capture(MakeQuitTask(), &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);

  // The commit should be marked as synced.
  EXPECT_EQ(0u, GetUnsyncedCommits().size());
}

// Verifies that GetUnsyncedCommits() returns commits ordered by their
// generation, and not by the timestamp.
//
// In this test the commits have the following structure:
//              (root)
//             /   |   \
//           (A)  (B)  (C)
//             \  /
//           (merge)
// C is the last commit to be created. The test verifies that the unsynced
// commits are returned in the generation order, with the merge commit being the
// last despite not being the most recent.
TEST_F(PageStorageTest, GetUnsyncedCommits) {
  const auto root_id = GetFirstHead()->GetId();

  Status status;
  std::unique_ptr<Journal> journal_a;
  storage_->StartCommit(root_id, JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal_a));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal_a->Put("a", RandomObjectId(), KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_a =
      TryCommitJournal(std::move(journal_a), Status::OK);
  EXPECT_TRUE(commit_a);
  EXPECT_EQ(1u, commit_a->GetGeneration());

  std::unique_ptr<Journal> journal_b;
  storage_->StartCommit(root_id, JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal_b));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal_b->Put("b", RandomObjectId(), KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_b =
      TryCommitJournal(std::move(journal_b), Status::OK);
  EXPECT_TRUE(commit_b);
  EXPECT_EQ(1u, commit_b->GetGeneration());

  std::unique_ptr<Journal> journal_merge;
  storage_->StartMergeCommit(
      commit_a->GetId(), commit_b->GetId(),
      callback::Capture(MakeQuitTask(), &status, &journal_merge));
  EXPECT_EQ(storage::Status::OK, status);

  std::unique_ptr<const Commit> commit_merge =
      TryCommitJournal(std::move(journal_merge), Status::OK);
  EXPECT_EQ(2u, commit_merge->GetGeneration());

  std::unique_ptr<Journal> journal_c;
  storage_->StartCommit(root_id, JournalType::EXPLICIT,
                        callback::Capture(MakeQuitTask(), &status, &journal_c));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(Status::OK,
            journal_c->Put("c", RandomObjectId(), KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_c =
      TryCommitJournal(std::move(journal_c), Status::OK);
  EXPECT_TRUE(commit_c);
  EXPECT_EQ(1u, commit_c->GetGeneration());

  // Verify that the merge commit is returned as last, even though commit C is
  // older.
  std::vector<std::unique_ptr<const Commit>> unsynced_commits =
      GetUnsyncedCommits();
  EXPECT_EQ(4u, unsynced_commits.size());
  EXPECT_EQ(commit_merge->GetId(), unsynced_commits.back()->GetId());
  EXPECT_LT(commit_merge->GetTimestamp(), commit_c->GetTimestamp());
}

}  // namespace

}  // namespace storage
