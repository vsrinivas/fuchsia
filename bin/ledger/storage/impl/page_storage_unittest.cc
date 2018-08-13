// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <chrono>
#include <memory>
#include <queue>
#include <set>

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/strings/string_printf.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/impl/btree/encoding.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/impl/commit_impl.h"
#include "peridot/bin/ledger/storage/impl/commit_random_impl.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/journal_impl.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/bin/ledger/storage/impl/page_db_empty_impl.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/impl/split.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/testing/test_with_environment.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace storage {

class PageStorageImplAccessorForTest {
 public:
  static void AddPiece(const std::unique_ptr<PageStorageImpl>& storage,
                       ObjectIdentifier object_identifier, ChangeSource source,
                       IsObjectSynced is_object_synced,
                       std::unique_ptr<DataSource::DataChunk> chunk,
                       fit::function<void(Status)> callback) {
    storage->AddPiece(std::move(object_identifier), source, is_object_synced,
                      std::move(chunk), std::move(callback));
  }

  static PageDb& GetDb(const std::unique_ptr<PageStorageImpl>& storage) {
    return *(storage->db_);
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

// DataSource that returns an error on the callback to Get().
class FakeErrorDataSource : public DataSource {
 public:
  explicit FakeErrorDataSource(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher) {}

  uint64_t GetSize() override { return 1; }

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(nullptr, DataSource::Status::ERROR);
    });
  }

  async_dispatcher_t* const dispatcher_;
};

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
      fit::function<void(fit::closure)> on_get_object)
      : on_get_object_(std::move(on_get_object)) {}

  void AddObject(ObjectIdentifier object_identifier, const std::string& value) {
    digest_to_value_[std::move(object_identifier)] = value;
  }

  void GetObject(ObjectIdentifier object_identifier,
                 fit::function<void(Status, ChangeSource, IsObjectSynced,
                                    std::unique_ptr<DataSource::DataChunk>)>
                     callback) override {
    std::string& value = digest_to_value_[object_identifier];
    object_requests.insert(std::move(object_identifier));
    on_get_object_([callback = std::move(callback), value] {
      callback(Status::OK, ChangeSource::CLOUD, IsObjectSynced::YES,
               DataSource::DataChunk::Create(value));
    });
  }

  std::set<ObjectIdentifier> object_requests;

 private:
  fit::function<void(fit::closure)> on_get_object_;
  std::map<ObjectIdentifier, std::string> digest_to_value_;
};

class FakeSyncDelegate : public DelayingFakeSyncDelegate {
 public:
  FakeSyncDelegate()
      : DelayingFakeSyncDelegate([](fit::closure callback) { callback(); }) {}
};

// Implements |Init()|, |CreateJournalId() and |StartBatch()| and fails with a
// |NOT_IMPLEMENTED| error in all other cases.
class FakePageDbImpl : public PageDbEmptyImpl {
 public:
  FakePageDbImpl() {}

  Status Init(CoroutineHandler* /*handler*/) override { return Status::OK; }
  Status CreateJournalId(CoroutineHandler* /*handler*/,
                         JournalType /*journal_type*/, const CommitId& /*base*/,
                         JournalId* journal_id) override {
    *journal_id = RandomString(10);
    return Status::OK;
  }

  Status StartBatch(CoroutineHandler* /*handler*/,
                    std::unique_ptr<PageDb::Batch>* batch) override {
    *batch = std::make_unique<FakePageDbImpl>();
    return Status::OK;
  }
};

class PageStorageTest : public ledger::TestWithEnvironment {
 public:
  PageStorageTest() : encryption_service_(dispatcher()) {}

  ~PageStorageTest() override {}

  // Test:
  void SetUp() override { ResetStorage(); }

  void ResetStorage() {
    if (storage_) {
      storage_->SetSyncDelegate(nullptr);
      storage_.reset();
    }
    tmpfs_ = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
    PageId id = RandomString(10);
    storage_ = std::make_unique<PageStorageImpl>(
        &environment_, &encryption_service_,
        ledger::DetachedPath(tmpfs_->root_fd()), id);

    bool called;
    Status status;
    storage_->Init(
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  PageStorage* GetStorage() { return storage_.get(); }

  std::vector<CommitId> GetHeads() {
    bool called;
    Status status;
    std::vector<CommitId> ids;
    storage_->GetHeadCommitIds(
        callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return ids;
  }

  std::unique_ptr<const Commit> GetFirstHead() {
    std::vector<CommitId> ids = GetHeads();
    EXPECT_FALSE(ids.empty());
    return GetCommit(ids[0]);
  }

  std::unique_ptr<const Commit> GetCommit(const CommitId& id) {
    bool called;
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->GetCommit(id, callback::Capture(callback::SetWhenCalled(&called),
                                              &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commit;
  }

  ::testing::AssertionResult PutInJournal(Journal* journal,
                                          const std::string& key,
                                          ObjectIdentifier object_identifier,
                                          KeyPriority priority) {
    bool called;
    Status status;
    journal->Put(key, std::move(object_identifier), priority,
                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "Journal::Put for key " << key << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "Journal::Put for key " << key
                                           << " returned status: " << status;
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult DeleteFromJournal(Journal* journal,
                                               const std::string& key) {
    bool called;
    Status status;
    journal->Delete(
        key, callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "Journal::Delete for key " << key << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "Journal::Delete for key " << key
                                           << " returned status: " << status;
    }
    return ::testing::AssertionSuccess();
  }

  std::unique_ptr<const Commit> TryCommitFromSync() {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), root_identifier, std::move(parent));

    bool called;
    Status status;
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commit;
  }

  // Returns an empty pointer if |CommitJournal| times out.
  FXL_WARN_UNUSED_RESULT std::unique_ptr<const Commit> TryCommitJournal(
      std::unique_ptr<Journal> journal, Status expected_status) {
    bool called;
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->CommitJournal(
        std::move(journal),
        callback::Capture(callback::SetWhenCalled(&called), &status, &commit));

    RunLoopUntilIdle();
    EXPECT_EQ(expected_status, status);
    if (!called) {
      return std::unique_ptr<const Commit>();
    }
    return commit;
  }

  // Returns an empty pointer if |TryCommitJournal| failed.
  FXL_WARN_UNUSED_RESULT std::unique_ptr<const Commit> TryCommitFromLocal(
      JournalType type, int keys, size_t min_key_size = 0) {
    bool called;
    Status status;
    std::unique_ptr<Journal> journal;
    storage_->StartCommit(
        GetFirstHead()->GetId(), type,
        callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_NE(nullptr, journal);

    for (int i = 0; i < keys; ++i) {
      auto key = fxl::StringPrintf("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      EXPECT_TRUE(PutInJournal(journal.get(), key, RandomObjectIdentifier(),
                               KeyPriority::EAGER));
    }

    EXPECT_TRUE(DeleteFromJournal(journal.get(), "key_does_not_exist"));

    std::unique_ptr<const Commit> commit =
        TryCommitJournal(std::move(journal), Status::OK);
    if (!commit) {
      return commit;
    }

    // Check the contents.
    std::vector<Entry> entries = GetCommitContents(*commit);
    EXPECT_EQ(static_cast<size_t>(keys), entries.size());
    for (int i = 0; i < keys; ++i) {
      auto key = fxl::StringPrintf("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      EXPECT_EQ(key, entries[i].key);
    }

    return commit;
  }

  void TryAddFromLocal(std::string content,
                       const ObjectIdentifier& expected_identifier) {
    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        DataSource::Create(std::move(content)),
        callback::Capture(callback::SetWhenCalled(&called), &status,
                          &object_identifier));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(expected_identifier, object_identifier);
  }

  std::unique_ptr<const Object> TryGetObject(
      const ObjectIdentifier& object_identifier, PageStorage::Location location,
      Status expected_status = Status::OK) {
    bool called;
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetObject(
        object_identifier, location,
        callback::Capture(callback::SetWhenCalled(&called), &status, &object));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(expected_status, status);
    return object;
  }

  std::unique_ptr<const Object> TryGetPiece(
      const ObjectIdentifier& object_identifier,
      Status expected_status = Status::OK) {
    bool called;
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetPiece(
        object_identifier,
        callback::Capture(callback::SetWhenCalled(&called), &status, &object));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(expected_status, status);
    return object;
  }

  std::vector<Entry> GetCommitContents(const Commit& commit) {
    bool called;
    Status status;
    std::vector<Entry> result;
    auto on_next = [&result](Entry e) {
      result.push_back(e);
      return true;
    };
    storage_->GetCommitContents(
        commit, "", std::move(on_next),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return result;
  }

  std::vector<std::unique_ptr<const Commit>> GetUnsyncedCommits() {
    bool called;
    Status status;
    std::vector<std::unique_ptr<const Commit>> commits;
    storage_->GetUnsyncedCommits(
        callback::Capture(callback::SetWhenCalled(&called), &status, &commits));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commits;
  }

  Status WriteObject(
      CoroutineHandler* handler, ObjectData* data,
      PageDbObjectStatus object_status = PageDbObjectStatus::TRANSIENT) {
    return PageStorageImplAccessorForTest::GetDb(storage_).WriteObject(
        handler, data->object_identifier, data->ToChunk(), object_status);
  }

  Status ReadObject(CoroutineHandler* handler,
                    ObjectIdentifier object_identifier,
                    std::unique_ptr<const Object>* object) {
    return PageStorageImplAccessorForTest::GetDb(storage_).ReadObject(
        handler, object_identifier, object);
  }

  ::testing::AssertionResult ObjectIsUntracked(
      ObjectIdentifier object_identifier, bool expected_untracked) {
    bool called;
    Status status;
    bool is_untracked;
    storage_->ObjectIsUntracked(
        object_identifier, callback::Capture(callback::SetWhenCalled(&called),
                                             &status, &is_untracked));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "ObjectIsUntracked for id " << object_identifier
             << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "ObjectIsUntracked for id " << object_identifier
             << " returned status " << status;
    }
    if (is_untracked != expected_untracked) {
      return ::testing::AssertionFailure()
             << "For id " << object_identifier
             << " expected to find the object " << (is_untracked ? "un" : "")
             << "tracked, but was " << (expected_untracked ? "un" : "")
             << "tracked, instead.";
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult IsPieceSynced(ObjectIdentifier object_identifier,
                                           bool expected_synced) {
    bool called;
    Status status;
    bool is_synced;
    storage_->IsPieceSynced(object_identifier,
                            callback::Capture(callback::SetWhenCalled(&called),
                                              &status, &is_synced));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "IsPieceSynced for id " << object_identifier
             << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "IsPieceSynced for id " << object_identifier
             << " returned status " << status;
    }
    if (is_synced != expected_synced) {
      return ::testing::AssertionFailure()
             << "For id " << object_identifier
             << " expected to find the object " << (is_synced ? "un" : "")
             << "synced, but was " << (expected_synced ? "un" : "")
             << "synced, instead.";
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult CreateNodeFromIdentifier(
      ObjectIdentifier identifier,
      std::unique_ptr<const btree::TreeNode>* node) {
    bool called;
    Status status;
    std::unique_ptr<const btree::TreeNode> result;
    btree::TreeNode::FromIdentifier(
        GetStorage(), std::move(identifier),
        callback::Capture(callback::SetWhenCalled(&called), &status, &result));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "TreeNode::FromDigest callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "TreeNode::FromDigest failed with status " << status;
    }
    node->swap(result);
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult CreateNodeFromEntries(
      const std::vector<Entry>& entries,
      const std::map<size_t, ObjectIdentifier>& children,
      std::unique_ptr<const btree::TreeNode>* node) {
    bool called;
    Status status;
    ObjectIdentifier identifier;
    btree::TreeNode::FromEntries(
        GetStorage(), 0u, entries, children,
        callback::Capture(callback::SetWhenCalled(&called), &status,
                          &identifier));
    RunLoopUntilIdle();
    if (!called) {
      return ::testing::AssertionFailure()
             << "TreeNode::FromEntries callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "TreeNode::FromEntries failed with status " << status;
    }
    return CreateNodeFromIdentifier(identifier, node);
  }

  ::testing::AssertionResult GetEmptyNodeIdentifier(
      ObjectIdentifier* empty_node_identifier) {
    bool called;
    Status status;
    btree::TreeNode::Empty(GetStorage(),
                           callback::Capture(callback::SetWhenCalled(&called),
                                             &status, empty_node_identifier));
    RunLoopUntilIdle();
    if (!called) {
      return ::testing::AssertionFailure()
             << "TreeNode::Empty callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "TreeNode::Empty failed with status " << status;
    }
    return ::testing::AssertionSuccess();
  }

  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  std::unique_ptr<PageStorageImpl> storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageStorageTest);
};

TEST_F(PageStorageTest, AddGetLocalCommits) {
  // Search for a commit id that doesn't exist and see the error.
  bool called;
  Status status;
  std::unique_ptr<const Commit> lookup_commit;
  storage_->GetCommit(RandomCommitId(),
                      callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &lookup_commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::NOT_FOUND, status);
  EXPECT_FALSE(lookup_commit);

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  // Search for a commit that exist and check the content.
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  std::unique_ptr<const Commit> found = GetCommit(id);
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddCommitFromLocalDoNotMarkUnsynedAlreadySyncedCommit) {
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      commit->Clone(), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  auto commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(id, commits[0]->GetId());

  storage_->MarkCommitSynced(
      id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Add the commit again.
  storage_->AddCommitFromLocal(
      commit->Clone(), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Check that the commit is not marked unsynced.
  commits = GetUnsyncedCommits();
  EXPECT_EQ(0u, commits.size());
}

TEST_F(PageStorageTest, AddCommitBeforeParentsError) {
  // Try to add a commit before its parent and see the error.
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(new CommitRandomImpl());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::ILLEGAL_STATE, status);
}

TEST_F(PageStorageTest, AddCommitsOutOfOrder) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit1 = CommitImpl::FromContentAndParents(
      storage_.get(), root_identifier, std::move(parent));
  parent.clear();
  parent.push_back(commit1->Clone());
  auto commit2 = CommitImpl::FromContentAndParents(
      storage_.get(), root_identifier, std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit2->GetId(),
                                 commit2->GetStorageBytes().ToString());
  commits_and_bytes.emplace_back(commit1->GetId(),
                                 commit1->GetStorageBytes().ToString());

  bool called;
  Status status;
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageStorageTest, AddGetSyncedCommits) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    FakeSyncDelegate sync;
    storage_->SetSyncDelegate(&sync);

    // Create a node with 2 values.
    ObjectData lazy_value("Some data", InlineBehavior::PREVENT);
    ObjectData eager_value("More data", InlineBehavior::PREVENT);
    std::vector<Entry> entries = {
        Entry{"key0", lazy_value.object_identifier, KeyPriority::LAZY},
        Entry{"key1", eager_value.object_identifier, KeyPriority::EAGER},
    };
    std::unique_ptr<const btree::TreeNode> node;
    ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
    ObjectIdentifier root_identifier = node->GetIdentifier();

    // Add the three objects to FakeSyncDelegate.
    sync.AddObject(lazy_value.object_identifier, lazy_value.value);
    sync.AddObject(eager_value.object_identifier, eager_value.value);

    {
      // Ensure root_object is not kept, as the storage it depends on will be
      // deleted.
      std::unique_ptr<const Object> root_object =
          TryGetObject(root_identifier, PageStorage::Location::NETWORK);

      fxl::StringView root_data;
      ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
      sync.AddObject(root_identifier, root_data.ToString());
    }

    // Reset and clear the storage.
    ResetStorage();
    storage_->SetSyncDelegate(&sync);

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
        storage_.get(), root_identifier, std::move(parent));
    CommitId id = commit->GetId();

    // Adding the commit should only request the tree node and the eager value.
    sync.object_requests.clear();
    bool called;
    Status status;
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, sync.object_requests.size());
    EXPECT_TRUE(sync.object_requests.find(root_identifier) !=
                sync.object_requests.end());
    EXPECT_TRUE(sync.object_requests.find(eager_value.object_identifier) !=
                sync.object_requests.end());

    // Adding the same commit twice should not request any objects from sync.
    sync.object_requests.clear();
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(sync.object_requests.empty());

    std::unique_ptr<const Commit> found = GetCommit(id);
    EXPECT_EQ(commit->GetStorageBytes(), found->GetStorageBytes());

    // Check that the commit is not marked as unsynced.
    std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();
    EXPECT_TRUE(commits.empty());
  });
}

// Check that receiving a remote commit that is already present locally but not
// synced will mark the commit as synced.
TEST_F(PageStorageTest, MarkRemoteCommitSynced) {
  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);

  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::unique_ptr<const Object> root_object =
      TryGetObject(root_identifier, PageStorage::Location::NETWORK);

  fxl::StringView root_data;
  ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
  sync.AddObject(root_identifier, root_data.ToString());

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), root_identifier, std::move(parent));
  CommitId id = commit->GetId();

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(1u, GetUnsyncedCommits().size());
  storage_->GetCommit(id, callback::Capture(callback::SetWhenCalled(&called),
                                            &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(),
                                 commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(0u, GetUnsyncedCommits().size());
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();

  // Initially there should be no unsynced commits.
  EXPECT_TRUE(commits.empty());

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  // After adding a commit it should marked as unsynced.
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(storage_bytes, commits[0]->GetStorageBytes());

  // Mark it as synced.
  storage_->MarkCommitSynced(
      id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
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
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id = commit->GetId();

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  heads = GetHeads();
  ASSERT_EQ(1u, heads.size());
  EXPECT_EQ(id, heads[0]);
}

TEST_F(PageStorageTest, CreateJournals) {
  // Explicit journal.
  auto left_commit = TryCommitFromLocal(JournalType::EXPLICIT, 5);
  ASSERT_TRUE(left_commit);
  auto right_commit = TryCommitFromLocal(JournalType::IMPLICIT, 10);
  ASSERT_TRUE(right_commit);

  // Journal for merge commit.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartMergeCommit(
      left_commit->GetId(), right_commit->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_NE(nullptr, journal);

  storage_->RollbackJournal(
      std::move(journal),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
}

TEST_F(PageStorageTest, CreateJournalHugeNode) {
  std::unique_ptr<const Commit> commit =
      TryCommitFromLocal(JournalType::EXPLICIT, 500, 1024);
  ASSERT_TRUE(commit);
  std::vector<Entry> entries = GetCommitContents(*commit);

  EXPECT_EQ(500u, entries.size());
  for (const auto& entry : entries) {
    EXPECT_EQ(1024u, entry.key.size());
  }

  // Check that all node's parts are marked as unsynced.
  bool called;
  Status status;
  std::vector<ObjectIdentifier> object_identifiers;
  storage_->GetUnsyncedPieces(callback::Capture(
      callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  bool found_index = false;
  std::set<ObjectIdentifier> unsynced_identifiers(object_identifiers.begin(),
                                                  object_identifiers.end());
  for (const auto& identifier : unsynced_identifiers) {
    EXPECT_FALSE(GetObjectDigestType(identifier.object_digest) ==
                 ObjectDigestType::INLINE);

    if (GetObjectDigestType(identifier.object_digest) ==
        ObjectDigestType::INDEX_HASH) {
      found_index = true;
      std::set<ObjectIdentifier> sub_identifiers;
      IterationStatus iteration_status = IterationStatus::ERROR;
      CollectPieces(
          identifier,
          [this](ObjectIdentifier identifier,
                 fit::function<void(Status, fxl::StringView)> callback) {
            storage_->GetPiece(
                std::move(identifier),
                [callback = std::move(callback)](
                    Status status, std::unique_ptr<const Object> object) {
                  if (status != Status::OK) {
                    callback(status, "");
                    return;
                  }
                  fxl::StringView data;
                  status = object->GetData(&data);
                  callback(status, data);
                });
          },
          [&iteration_status, &sub_identifiers](IterationStatus status,
                                                ObjectIdentifier identifier) {
            iteration_status = status;
            if (status == IterationStatus::IN_PROGRESS) {
              EXPECT_TRUE(sub_identifiers.insert(identifier).second);
            }
            return true;
          });
      RunLoopUntilIdle();
      EXPECT_EQ(IterationStatus::DONE, iteration_status);
      for (const auto& identifier : sub_identifiers) {
        EXPECT_EQ(1u, unsynced_identifiers.count(identifier));
      }
    }
  }
  EXPECT_TRUE(found_index);
}

TEST_F(PageStorageTest, JournalCommitFailsAfterFailedOperation) {
  // Using FakePageDbImpl will cause all PageDb operations that have to do
  // with journal entry update, to fail with a NOT_IMPLEMENTED error.
  auto test_storage = std::make_unique<PageStorageImpl>(
      &environment_, &encryption_service_, std::make_unique<FakePageDbImpl>(),
      RandomString(10));

  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  // Explicit journals.
  // The first call will fail because FakePageDbImpl::AddJournalEntry()
  // returns an error. After a failed call all other Put/Delete/Commit
  // operations should fail with ILLEGAL_STATE.
  test_storage->StartCommit(
      RandomCommitId(), JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ObjectIdentifier random_identifier = RandomObjectIdentifier();

  journal->Put("key", random_identifier, KeyPriority::EAGER,
               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::OK, status);

  journal->Put("key", random_identifier, KeyPriority::EAGER,
               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::ILLEGAL_STATE, status);

  journal->Delete("key",
                  callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::ILLEGAL_STATE, status);

  journal->Clear(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::ILLEGAL_STATE, status);

  ASSERT_FALSE(TryCommitJournal(std::move(journal), Status::ILLEGAL_STATE));

  // Implicit journals.
  // All calls will fail because of FakePageDbImpl implementation, not because
  // of an ILLEGAL_STATE error.
  test_storage->StartCommit(
      RandomCommitId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  journal->Put("key", random_identifier, KeyPriority::EAGER,
               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::OK, status);

  journal->Put("key", random_identifier, KeyPriority::EAGER,
               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::ILLEGAL_STATE, status);

  journal->Delete("key",
                  callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::ILLEGAL_STATE, status);

  journal->Clear(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::ILLEGAL_STATE, status);

  std::unique_ptr<const Commit> commit;
  test_storage->CommitJournal(
      std::move(journal),
      callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_NE(Status::ILLEGAL_STATE, status);
}

TEST_F(PageStorageTest, DestroyUncommittedJournal) {
  // It is not an error if a journal is not committed or rolled back.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, journal);
  EXPECT_TRUE(PutInJournal(journal.get(), "key", RandomObjectIdentifier(),
                           KeyPriority::EAGER));
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        data.ToDataSource(), callback::Capture(callback::SetWhenCalled(&called),
                                               &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(data.object_identifier, object_identifier);

    std::unique_ptr<const Object> object;
    ASSERT_EQ(Status::OK, ReadObject(handler, object_identifier, &object));
    fxl::StringView content;
    ASSERT_EQ(Status::OK, object->GetData(&content));
    EXPECT_EQ(data.value, content);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(object_identifier, false));
  });
}

TEST_F(PageStorageTest, AddSmallObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data");

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        data.ToDataSource(), callback::Capture(callback::SetWhenCalled(&called),
                                               &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(data.object_identifier, object_identifier);
    EXPECT_EQ(data.value, object_identifier.object_digest);

    std::unique_ptr<const Object> object;
    EXPECT_EQ(Status::NOT_FOUND,
              ReadObject(handler, object_identifier, &object));
    // Inline objects do not need to ever be tracked.
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, false));
  });
}

TEST_F(PageStorageTest, InterruptAddObjectFromLocal) {
  ObjectData data("Some data");

  storage_->AddObjectFromLocal(
      data.ToDataSource(),
      [](Status returned_status, ObjectIdentifier object_identifier) {});

  // Checking that we do not crash when deleting the storage while an AddObject
  // call is in progress.
  storage_.reset();
}

TEST_F(PageStorageTest, AddObjectFromLocalError) {
  auto data_source = std::make_unique<FakeErrorDataSource>(dispatcher());
  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      std::move(data_source),
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::IO_ERROR, status);
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, false));
}

TEST_F(PageStorageTest, AddLocalPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.object_identifier, ChangeSource::LOCAL,
        IsObjectSynced::NO, data.ToChunk(),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Object> object;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &object));
    fxl::StringView content;
    ASSERT_EQ(Status::OK, object->GetData(&content));
    EXPECT_EQ(data.value, content);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));
  });
}

TEST_F(PageStorageTest, AddSyncPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.object_identifier, ChangeSource::CLOUD,
        IsObjectSynced::YES, data.ToChunk(),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Object> object;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &object));
    fxl::StringView content;
    ASSERT_EQ(Status::OK, object->GetData(&content));
    EXPECT_EQ(data.value, content);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, true));
  });
}

TEST_F(PageStorageTest, AddP2PPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.object_identifier, ChangeSource::P2P, IsObjectSynced::NO,
        data.ToChunk(),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Object> object;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &object));
    fxl::StringView content;
    ASSERT_EQ(Status::OK, object->GetData(&content));
    EXPECT_EQ(data.value, content);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));
  });
}

TEST_F(PageStorageTest, GetObject) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    std::unique_ptr<const Object> object =
        TryGetObject(data.object_identifier, PageStorage::Location::LOCAL);
    EXPECT_EQ(data.object_identifier, object->GetIdentifier());
    fxl::StringView object_data;
    ASSERT_EQ(Status::OK, object->GetData(&object_data));
    EXPECT_EQ(data.value, convert::ToString(object_data));
  });
}

TEST_F(PageStorageTest, GetObjectFromSync) {
  ObjectData data("Some data", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data.value);
  storage_->SetSyncDelegate(&sync);

  std::unique_ptr<const Object> object =
      TryGetObject(data.object_identifier, PageStorage::Location::NETWORK);
  EXPECT_EQ(data.object_identifier, object->GetIdentifier());
  fxl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data.value, convert::ToString(object_data));

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data("Some other data", InlineBehavior::PREVENT);
  TryGetObject(other_data.object_identifier, PageStorage::Location::LOCAL,
               Status::NOT_FOUND);
  TryGetObject(other_data.object_identifier, PageStorage::Location::NETWORK,
               Status::NOT_CONNECTED_ERROR);
}

TEST_F(PageStorageTest, GetObjectFromSyncWrongId) {
  ObjectData data("Some data", InlineBehavior::PREVENT);
  ObjectData data2("Some data2", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data2.value);
  storage_->SetSyncDelegate(&sync);

  TryGetObject(data.object_identifier, PageStorage::Location::NETWORK,
               Status::OBJECT_DIGEST_MISMATCH);
}

TEST_F(PageStorageTest, AddAndGetHugeObjectFromLocal) {
  std::string data_str = RandomString(65536);

  ObjectData data(std::move(data_str), InlineBehavior::PREVENT);

  ASSERT_EQ(ObjectDigestType::INDEX_HASH,
            GetObjectDigestType(data.object_identifier.object_digest));

  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      data.ToDataSource(), callback::Capture(callback::SetWhenCalled(&called),
                                             &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_identifier, object_identifier);

  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::LOCAL);
  fxl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
  EXPECT_TRUE(IsPieceSynced(object_identifier, false));

  // Check that the object is encoded with an index, and is different than the
  // piece obtained at |object_identifier|.
  std::unique_ptr<const Object> piece = TryGetPiece(object_identifier);
  fxl::StringView piece_content;
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
    TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));
  }

  std::vector<CommitId> commits;

  // Add one key-value pair per commit.
  for (size_t i = 0; i < size; ++i) {
    bool called;
    Status status;
    std::unique_ptr<Journal> journal;
    storage_->StartCommit(
        GetFirstHead()->GetId(), JournalType::IMPLICIT,
        callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    EXPECT_TRUE(PutInJournal(journal.get(), fxl::StringPrintf("key%lu", i),
                             data_array[i].object_identifier,
                             KeyPriority::LAZY));
    EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
    commits.push_back(GetFirstHead()->GetId());
  }

  // GetUnsyncedPieces should return the ids of all objects: 3 values and
  // the 3 root nodes of the 3 commits.
  bool called;
  Status status;
  std::vector<ObjectIdentifier> object_identifiers;
  storage_->GetUnsyncedPieces(callback::Capture(
      callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(6u, object_identifiers.size());
  for (size_t i = 0; i < size; ++i) {
    std::unique_ptr<const Commit> commit = GetCommit(commits[i]);
    EXPECT_TRUE(std::find_if(object_identifiers.begin(),
                             object_identifiers.end(),
                             [&](const auto& identifier) {
                               return identifier == commit->GetRootIdentifier();
                             }) != object_identifiers.end());
  }
  for (auto& data : data_array) {
    EXPECT_TRUE(std::find(object_identifiers.begin(), object_identifiers.end(),
                          data.object_identifier) != object_identifiers.end());
  }

  // Mark the 2nd object as synced. We now expect to still find the 2 unsynced
  // values and the (also unsynced) root node.
  storage_->MarkPieceSynced(
      data_array[1].object_identifier,
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  std::vector<ObjectIdentifier> objects;
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &objects));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(5u, objects.size());
  std::unique_ptr<const Commit> commit = GetCommit(commits[2]);
  EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                        commit->GetRootIdentifier()) != objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                        data_array[0].object_identifier) != objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(),
                        data_array[2].object_identifier) != objects.end());
}

TEST_F(PageStorageTest, PageIsSynced) {
  ObjectData data_array[] = {
      ObjectData("Some data", InlineBehavior::PREVENT),
      ObjectData("Some more data", InlineBehavior::PREVENT),
      ObjectData("Even more data", InlineBehavior::PREVENT),
  };
  constexpr size_t size = arraysize(data_array);
  for (auto& data : data_array) {
    TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));
  }

  // The objects have not been added in a commit: there is nothing to sync and
  // the page is considered synced.
  bool called;
  Status status;
  bool is_synced;
  storage_->IsSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(true, is_synced);

  // Add all objects in one commit.
  called = false;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_TRUE(PutInJournal(journal.get(), fxl::StringPrintf("key%lu", i),
                             data_array[i].object_identifier,
                             KeyPriority::LAZY));
  }
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  CommitId commit_id = GetFirstHead()->GetId();

  // After commiting, the page is unsynced.
  called = false;
  storage_->IsSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_synced);
  // Mark objects (and the root tree node) as synced and expect that the page is
  // still unsynced.
  for (const auto& data : data_array) {
    called = false;
    storage_->MarkPieceSynced(
        data.object_identifier,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }

  called = false;
  storage_->MarkPieceSynced(
      GetFirstHead()->GetRootIdentifier(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  called = false;
  storage_->IsSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_synced);

  // Mark the commit as synced and expect that the page is synced.
  called = false;
  storage_->MarkCommitSynced(
      commit_id, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  called = false;
  storage_->IsSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(is_synced);

  // All objects should be synced now.
  for (auto& data : data_array) {
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, true));
  }
}

TEST_F(PageStorageTest, PageIsMarkedOnlineAfterCloudSync) {
  // Check that the page is initially not marked as online.
  EXPECT_FALSE(storage_->IsOnline());

  // Create a local commit: the page is still not online.
  int size = 10;
  std::unique_ptr<const Commit> commit =
      TryCommitFromLocal(JournalType::EXPLICIT, size);
  EXPECT_FALSE(storage_->IsOnline());

  // Mark all objects as synced. The page is still not online: other devices
  // will only see these objects if the corresponding commit is also synced to
  // the cloud.
  bool called;
  Status status;
  std::vector<ObjectIdentifier> object_identifiers;
  storage_->GetUnsyncedPieces(callback::Capture(
      callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  for (ObjectIdentifier& object_identifier : object_identifiers) {
    storage_->MarkPieceSynced(
        object_identifier,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }
  EXPECT_FALSE(storage_->IsOnline());

  // Mark the commit as synced. The page should now be marked as online.
  storage_->MarkCommitSynced(
      commit->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(storage_->IsOnline());
}

TEST_F(PageStorageTest, PageIsMarkedOnlineSyncWithPeer) {
  // Check that the page is initially not marked as online.
  EXPECT_FALSE(storage_->IsOnline());

  // Mark the page as synced to peer and expect that it is marked as online.
  bool called;
  Status status;
  storage_->MarkSyncedToPeer(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(storage_->IsOnline());
}

TEST_F(PageStorageTest, PageIsEmpty) {
  ObjectData value("Some value", InlineBehavior::PREVENT);
  bool called;
  Status status;
  bool is_empty;

  // Initially the page is empty.
  storage_->IsEmpty(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(is_empty);

  // Add an entry and expect that the page is not empty any more.
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal.get(), "key", value.object_identifier,
                           KeyPriority::LAZY));
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_empty);

  // Clear the page and expect it to be empty again.
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(DeleteFromJournal(journal.get(), "key"));
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(is_empty);
}

TEST_F(PageStorageTest, UntrackedObjectsSimple) {
  ObjectData data("Some data", InlineBehavior::PREVENT);

  // The object is not yet created and its id should not be marked as untracked.
  EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));

  // After creating the object it should be marked as untracked.
  TryAddFromLocal(data.value, data.object_identifier);
  EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));

  // After adding the object in a commit it should not be untracked any more.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal.get(), "key", data.object_identifier,
                           KeyPriority::EAGER));
  EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
}

TEST_F(PageStorageTest, UntrackedObjectsComplex) {
  ObjectData data_array[] = {
      ObjectData("Some data", InlineBehavior::PREVENT),
      ObjectData("Some more data", InlineBehavior::PREVENT),
      ObjectData("Even more data", InlineBehavior::PREVENT),
  };
  for (auto& data : data_array) {
    TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
  }

  // Add a first commit containing data_array[0].
  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal.get(), "key0",
                           data_array[0].object_identifier, KeyPriority::LAZY));
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, true));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, false));
  EXPECT_TRUE(ObjectIsUntracked(data_array[1].object_identifier, true));
  EXPECT_TRUE(ObjectIsUntracked(data_array[2].object_identifier, true));

  // Create a second commit. After calling Put for "key1" for the second time
  // data_array[1] is no longer part of this commit: it should remain
  // untracked after committing.
  journal.reset();
  storage_->StartCommit(
      GetFirstHead()->GetId(), JournalType::IMPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal.get(), "key1",
                           data_array[1].object_identifier, KeyPriority::LAZY));
  EXPECT_TRUE(PutInJournal(journal.get(), "key2",
                           data_array[2].object_identifier, KeyPriority::LAZY));
  EXPECT_TRUE(PutInJournal(journal.get(), "key1",
                           data_array[2].object_identifier, KeyPriority::LAZY));
  EXPECT_TRUE(PutInJournal(journal.get(), "key3",
                           data_array[0].object_identifier, KeyPriority::LAZY));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, false));
  EXPECT_TRUE(ObjectIsUntracked(data_array[1].object_identifier, true));
  EXPECT_TRUE(ObjectIsUntracked(data_array[2].object_identifier, false));
}

TEST_F(PageStorageTest, CommitWatchers) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  // Add a watcher and receive the commit.
  auto expected = TryCommitFromLocal(JournalType::EXPLICIT, 10);
  ASSERT_TRUE(expected);
  EXPECT_EQ(1, watcher.commit_count);
  EXPECT_EQ(expected->GetId(), watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);

  // Add a second watcher.
  FakeCommitWatcher watcher2;
  storage_->AddCommitWatcher(&watcher2);
  expected = TryCommitFromLocal(JournalType::IMPLICIT, 10);
  ASSERT_TRUE(expected);
  EXPECT_EQ(2, watcher.commit_count);
  EXPECT_EQ(expected->GetId(), watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);
  EXPECT_EQ(1, watcher2.commit_count);
  EXPECT_EQ(expected->GetId(), watcher2.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher2.last_source);

  // Remove one watcher.
  storage_->RemoveCommitWatcher(&watcher2);
  expected = TryCommitFromSync();
  EXPECT_EQ(3, watcher.commit_count);
  EXPECT_EQ(expected->GetId(), watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::CLOUD, watcher.last_source);
  EXPECT_EQ(1, watcher2.commit_count);
}

TEST_F(PageStorageTest, SyncMetadata) {
  std::vector<std::pair<fxl::StringView, fxl::StringView>> keys_and_values = {
      {"foo1", "foo2"}, {"bar1", " bar2 "}};
  for (const auto& key_and_value : keys_and_values) {
    auto key = key_and_value.first;
    auto value = key_and_value.second;
    bool called;
    Status status;
    std::string returned_value;
    storage_->GetSyncMetadata(
        key, callback::Capture(callback::SetWhenCalled(&called), &status,
                               &returned_value));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::NOT_FOUND, status);

    storage_->SetSyncMetadata(
        key, value,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    storage_->GetSyncMetadata(
        key, callback::Capture(callback::SetWhenCalled(&called), &status,
                               &returned_value));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(value, returned_value);
  }
}

TEST_F(PageStorageTest, AddMultipleCommitsFromSync) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    FakeSyncDelegate sync;
    storage_->SetSyncDelegate(&sync);

    // Build the commit Tree with:
    //         0
    //         |
    //         1  2
    std::vector<ObjectIdentifier> object_identifiers;
    object_identifiers.resize(3);
    for (size_t i = 0; i < object_identifiers.size(); ++i) {
      ObjectData value("value" + std::to_string(i), InlineBehavior::PREVENT);
      std::vector<Entry> entries = {Entry{"key" + std::to_string(i),
                                          value.object_identifier,
                                          KeyPriority::EAGER}};
      std::unique_ptr<const btree::TreeNode> node;
      ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
      object_identifiers[i] = node->GetIdentifier();
      sync.AddObject(value.object_identifier, value.value);
      std::unique_ptr<const Object> root_object =
          TryGetObject(object_identifiers[i], PageStorage::Location::NETWORK);
      fxl::StringView root_data;
      ASSERT_EQ(Status::OK, root_object->GetData(&root_data));
      sync.AddObject(object_identifiers[i], root_data.ToString());
    }

    // Reset and clear the storage.
    ResetStorage();
    storage_->SetSyncDelegate(&sync);

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit0 = CommitImpl::FromContentAndParents(
        storage_.get(), object_identifiers[0], std::move(parent));
    parent.clear();

    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit1 = CommitImpl::FromContentAndParents(
        storage_.get(), object_identifiers[1], std::move(parent));
    parent.clear();

    parent.emplace_back(commit1->Clone());
    std::unique_ptr<const Commit> commit2 = CommitImpl::FromContentAndParents(
        storage_.get(), object_identifiers[2], std::move(parent));

    std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
    commits_and_bytes.emplace_back(commit0->GetId(),
                                   commit0->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit1->GetId(),
                                   commit1->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit2->GetId(),
                                   commit2->GetStorageBytes().ToString());

    bool called;
    Status status;
    storage_->AddCommitsFromSync(
        std::move(commits_and_bytes), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    EXPECT_EQ(4u, sync.object_requests.size());
    EXPECT_NE(sync.object_requests.find(object_identifiers[0]),
              sync.object_requests.end());
    EXPECT_EQ(sync.object_requests.find(object_identifiers[1]),
              sync.object_requests.end());
    EXPECT_NE(sync.object_requests.find(object_identifiers[2]),
              sync.object_requests.end());
  });
}

TEST_F(PageStorageTest, Generation) {
  std::unique_ptr<const Commit> commit1 =
      TryCommitFromLocal(JournalType::EXPLICIT, 3);
  ASSERT_TRUE(commit1);
  EXPECT_EQ(1u, commit1->GetGeneration());

  std::unique_ptr<const Commit> commit2 =
      TryCommitFromLocal(JournalType::EXPLICIT, 3);
  ASSERT_TRUE(commit2);
  EXPECT_EQ(2u, commit2->GetGeneration());

  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartMergeCommit(
      commit1->GetId(), commit2->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  std::unique_ptr<const Commit> commit3 =
      TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(commit3);
  EXPECT_EQ(3u, commit3->GetGeneration());
}

TEST_F(PageStorageTest, GetEntryFromCommit) {
  int size = 10;
  std::unique_ptr<const Commit> commit =
      TryCommitFromLocal(JournalType::EXPLICIT, size);
  ASSERT_TRUE(commit);

  bool called;
  Status status;
  Entry entry;
  storage_->GetEntryFromCommit(
      *commit, "key not found",
      callback::Capture(callback::SetWhenCalled(&called), &status, &entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::NOT_FOUND, status);

  for (int i = 0; i < size; ++i) {
    std::string expected_key = fxl::StringPrintf("key%05d", i);
    storage_->GetEntryFromCommit(
        *commit, expected_key,
        callback::Capture(callback::SetWhenCalled(&called), &status, &entry));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(Status::OK, status);
    EXPECT_EQ(expected_key, entry.key);
  }
}

TEST_F(PageStorageTest, WatcherForReEntrantCommits) {
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());

  std::unique_ptr<const Commit> commit1 = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id1 = commit1->GetId();

  parent.clear();
  parent.emplace_back(commit1->Clone());

  std::unique_ptr<const Commit> commit2 = CommitImpl::FromContentAndParents(
      storage_.get(), RandomObjectIdentifier(), std::move(parent));
  CommitId id2 = commit2->GetId();

  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  bool called;
  Status status;
  storage_->AddCommitFromLocal(
      std::move(commit1), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  storage_->AddCommitFromLocal(
      std::move(commit2), {},
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(2, watcher.commit_count);
  EXPECT_EQ(id2, watcher.last_commit_id);
}

TEST_F(PageStorageTest, NoOpCommit) {
  std::vector<CommitId> heads = GetHeads();
  ASSERT_FALSE(heads.empty());

  bool called;
  Status status;
  std::unique_ptr<Journal> journal;
  storage_->StartCommit(
      heads[0], JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Create a key, and delete it.
  EXPECT_TRUE(PutInJournal(journal.get(), "key", RandomObjectIdentifier(),
                           KeyPriority::EAGER));
  EXPECT_TRUE(DeleteFromJournal(journal.get(), "key"));

  // Commit the journal.
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(
      std::move(journal),
      callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  ASSERT_EQ(Status::OK, status);
  ASSERT_TRUE(commit);
  // Expect that the commit id is the same as the original one.
  EXPECT_EQ(heads[0], commit->GetId());
}

// Check that receiving a remote commit and commiting locally at the same time
// do not prevent the commit to be marked as unsynced.
TEST_F(PageStorageTest, MarkRemoteCommitSyncedRace) {
  bool sync_delegate_called;
  fit::closure sync_delegate_call;
  DelayingFakeSyncDelegate sync(callback::Capture(
      callback::SetWhenCalled(&sync_delegate_called), &sync_delegate_call));
  storage_->SetSyncDelegate(&sync);

  // We need to create new nodes for the storage to be asynchronous. The empty
  // node is already there, so we create two (child, which is empty, and root,
  // which contains child).
  std::string child_data = btree::EncodeNode(0u, std::vector<Entry>(), {});
  ObjectIdentifier child_identifier = encryption_service_.MakeObjectIdentifier(
      ComputeObjectDigest(ObjectType::VALUE, child_data));
  sync.AddObject(child_identifier, child_data);

  std::string root_data =
      btree::EncodeNode(0u, std::vector<Entry>(), {{0u, child_identifier}});
  ObjectIdentifier root_identifier = encryption_service_.MakeObjectIdentifier(
      ComputeObjectDigest(ObjectType::VALUE, root_data));
  sync.AddObject(root_identifier, root_data);

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());

  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_.get(), root_identifier, std::move(parent));
  CommitId id = commit->GetId();

  // Start adding the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(),
                                 commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                        &commits_from_sync_status));

  // Make the loop run until GetObject is called in sync, and before
  // AddCommitsFromSync finishes.
  RunLoopUntilIdle();
  EXPECT_TRUE(sync_delegate_called);
  EXPECT_FALSE(commits_from_sync_called);

  // Add the local commit.
  bool commits_from_local_called;
  Status commits_from_local_status;
  storage_->AddCommitFromLocal(
      std::move(commit), {},
      callback::Capture(callback::SetWhenCalled(&commits_from_local_called),
                        &commits_from_local_status));

  RunLoopUntilIdle();
  EXPECT_FALSE(commits_from_sync_called);
  // The local commit should be commited.
  EXPECT_TRUE(commits_from_local_called);
  ASSERT_TRUE(sync_delegate_call);
  sync_delegate_call();

  // Let the two AddCommit finish.
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_TRUE(commits_from_local_called);
  EXPECT_EQ(Status::OK, commits_from_sync_status);
  EXPECT_EQ(Status::OK, commits_from_local_status);

  // Verify that the commit is added correctly.
  bool called;
  Status status;
  storage_->GetCommit(id, callback::Capture(callback::SetWhenCalled(&called),
                                            &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
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
  const CommitId root_id = GetFirstHead()->GetId();

  bool called;
  Status status;
  std::unique_ptr<Journal> journal_a;
  storage_->StartCommit(
      root_id, JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal_a));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal_a.get(), "a", RandomObjectIdentifier(),
                           KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_a =
      TryCommitJournal(std::move(journal_a), Status::OK);
  ASSERT_TRUE(commit_a);
  EXPECT_EQ(1u, commit_a->GetGeneration());

  std::unique_ptr<Journal> journal_b;
  storage_->StartCommit(
      root_id, JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal_b));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal_b.get(), "b", RandomObjectIdentifier(),
                           KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_b =
      TryCommitJournal(std::move(journal_b), Status::OK);
  ASSERT_TRUE(commit_b);
  EXPECT_EQ(1u, commit_b->GetGeneration());

  std::unique_ptr<Journal> journal_merge;
  storage_->StartMergeCommit(commit_a->GetId(), commit_b->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called),
                                               &status, &journal_merge));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);

  std::unique_ptr<const Commit> commit_merge =
      TryCommitJournal(std::move(journal_merge), Status::OK);
  ASSERT_TRUE(commit_merge);
  EXPECT_EQ(2u, commit_merge->GetGeneration());

  std::unique_ptr<Journal> journal_c;
  storage_->StartCommit(
      root_id, JournalType::EXPLICIT,
      callback::Capture(callback::SetWhenCalled(&called), &status, &journal_c));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(PutInJournal(journal_c.get(), "c", RandomObjectIdentifier(),
                           KeyPriority::EAGER));
  std::unique_ptr<const Commit> commit_c =
      TryCommitJournal(std::move(journal_c), Status::OK);
  ASSERT_TRUE(commit_c);
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
