// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/timekeeper/test_clock.h>

#include <chrono>
#include <memory>
#include <queue>
#include <set>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/commit_impl.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db_empty_impl.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/split.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace storage {

class PageStorageImplAccessorForTest {
 public:
  static void AddPiece(const std::unique_ptr<PageStorageImpl>& storage,
                       std::unique_ptr<Piece> piece, ChangeSource source,
                       IsObjectSynced is_object_synced, ObjectReferencesAndPriority references,
                       fit::function<void(Status)> callback) {
    storage->AddPiece(std::move(piece), source, is_object_synced, std::move(references),
                      std::move(callback));
  }

  static PageDb& GetDb(const std::unique_ptr<PageStorageImpl>& storage) { return *(storage->db_); }
};

namespace {

using ::coroutine::CoroutineHandler;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAreArray;

std::vector<PageStorage::CommitIdAndBytes> CommitAndBytesFromCommit(const Commit& commit) {
  std::vector<PageStorage::CommitIdAndBytes> result;
  result.emplace_back(commit.GetId(), commit.GetStorageBytes().ToString());
  return result;
}

// DataSource that returns an error on the callback to Get().
class FakeErrorDataSource : public DataSource {
 public:
  explicit FakeErrorDataSource(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  uint64_t GetSize() override { return 1; }

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback) override {
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
  explicit DelayingFakeSyncDelegate(fit::function<void(fit::closure)> on_get_object)
      : on_get_object_(std::move(on_get_object)) {}

  void AddObject(ObjectIdentifier object_identifier, const std::string& value) {
    digest_to_value_[std::move(object_identifier)] = value;
  }

  void GetObject(ObjectIdentifier object_identifier,
                 fit::function<void(Status, ChangeSource, IsObjectSynced,
                                    std::unique_ptr<DataSource::DataChunk>)>
                     callback) override {
    auto value_found = digest_to_value_.find(object_identifier);
    if (value_found == digest_to_value_.end()) {
      callback(Status::INTERNAL_NOT_FOUND, ChangeSource::CLOUD, IsObjectSynced::NO, nullptr);
      return;
    }
    std::string& value = value_found->second;
    object_requests.insert(std::move(object_identifier));
    on_get_object_([callback = std::move(callback), value] {
      callback(Status::OK, ChangeSource::CLOUD, IsObjectSynced::YES,
               DataSource::DataChunk::Create(value));
    });
  }

  size_t GetNumberOfObjectsStored() { return digest_to_value_.size(); }

  std::set<ObjectIdentifier> object_requests;

 private:
  fit::function<void(fit::closure)> on_get_object_;
  std::map<ObjectIdentifier, std::string> digest_to_value_;
};

class FakeSyncDelegate : public DelayingFakeSyncDelegate {
 public:
  FakeSyncDelegate() : DelayingFakeSyncDelegate([](fit::closure callback) { callback(); }) {}
};

// Shim for LevelDB that allows to selectively fail some calls.
class ControlledLevelDb : public Db {
 public:
  explicit ControlledLevelDb(async_dispatcher_t* dispatcher, ledger::DetachedPath db_path)
      : leveldb_(dispatcher, db_path) {}

  class ControlledBatch : public Batch {
   public:
    explicit ControlledBatch(ControlledLevelDb* controller, std::unique_ptr<Batch> batch)
        : controller_(controller), batch_(std::move(batch)) {}

    // Batch:
    Status Put(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
               fxl::StringView value) override {
      return batch_->Put(handler, key, value);
    }

    Status Delete(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
      return batch_->Delete(handler, key);
    }

    Status Execute(coroutine::CoroutineHandler* handler) override {
      if (controller_->fail_batch_execute_after_ == 0) {
        return Status::IO_ERROR;
      }
      if (controller_->fail_batch_execute_after_ > 0) {
        controller_->fail_batch_execute_after_--;
      }
      return batch_->Execute(handler);
    }

   private:
    ControlledLevelDb* controller_;
    std::unique_ptr<Batch> batch_;
  };

  Status Init() { return leveldb_.Init(); }

  // Sets the number of calls to |Batch::Execute()|, for batches generated by
  // this object, after which all calls would fail. It is used to simulate write
  // failures.
  // If |fail_batch_execute_after| is negative, or this method is not called,
  // |Batch::Execute()| calls will nevef fail.
  void SetFailBatchExecuteAfter(int fail_batch_execute_after) {
    fail_batch_execute_after_ = fail_batch_execute_after;
  }

  // Db:
  Status StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) override {
    std::unique_ptr<Batch> inner_batch;
    Status status = leveldb_.StartBatch(handler, &inner_batch);
    *batch = std::make_unique<ControlledBatch>(this, std::move(inner_batch));
    return status;
  }

  Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
             std::string* value) override {
    return leveldb_.Get(handler, key, value);
  }

  Status HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
    return leveldb_.HasKey(handler, key);
  }

  Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier,
                   std::unique_ptr<const Piece>* piece) override {
    return leveldb_.GetObject(handler, key, std::move(object_identifier), piece);
  }

  Status GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override {
    return leveldb_.GetByPrefix(handler, prefix, key_suffixes);
  }

  Status GetEntriesByPrefix(coroutine::CoroutineHandler* handler,
                            convert::ExtendedStringView prefix,
                            std::vector<std::pair<std::string, std::string>>* entries) override {
    return leveldb_.GetEntriesByPrefix(handler, prefix, entries);
  }

  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<
          Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>*
          iterator) override {
    return leveldb_.GetIteratorAtPrefix(handler, prefix, iterator);
  }

 private:
  // Number of calls to |Batch::Execute()| before they start failing. If
  // negative, |Batch::Execute()| calls will never fail.
  int fail_batch_execute_after_ = -1;
  LevelDb leveldb_;
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
    PageId id = RandomString(environment_.random(), 10);
    auto db =
        std::make_unique<ControlledLevelDb>(dispatcher(), ledger::DetachedPath(tmpfs_->root_fd()));
    leveldb_ = db.get();
    ASSERT_EQ(Status::OK, db->Init());
    storage_ = std::make_unique<PageStorageImpl>(&environment_, &encryption_service_, std::move(db),
                                                 id, CommitPruningPolicy::NEVER);

    bool called;
    Status status;
    storage_->Init(callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  PageStorage* GetStorage() { return storage_.get(); }

  std::vector<std::unique_ptr<const Commit>> GetHeads() {
    std::vector<std::unique_ptr<const Commit>> heads;
    Status status = storage_->GetHeadCommits(&heads);
    EXPECT_EQ(Status::OK, status);
    return heads;
  }

  std::unique_ptr<const Commit> GetFirstHead() {
    std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
    EXPECT_FALSE(heads.empty());
    return std::move(heads[0]);
  }

  std::unique_ptr<const Commit> GetCommit(const CommitId& id) {
    bool called;
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->GetCommit(id, callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commit;
  }

  std::unique_ptr<const Commit> TryCommitFromSync() {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
        storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));

    bool called;
    Status status;
    std::vector<CommitId> missing_ids;
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
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
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit));

    RunLoopUntilIdle();
    EXPECT_EQ(expected_status, status);
    if (!called) {
      return std::unique_ptr<const Commit>();
    }
    return commit;
  }

  // Returns an empty pointer if |TryCommitJournal| failed.
  FXL_WARN_UNUSED_RESULT std::unique_ptr<const Commit> TryCommitFromLocal(int keys,
                                                                          size_t min_key_size = 0) {
    std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());

    for (int i = 0; i < keys; ++i) {
      auto key = fxl::StringPrintf("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      journal->Put(key, RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
    }

    journal->Delete("key_does_not_exist");

    std::unique_ptr<const Commit> commit = TryCommitJournal(std::move(journal), Status::OK);
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

  void TryAddFromLocal(std::string content, const ObjectIdentifier& expected_identifier) {
    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, DataSource::Create(std::move(content)), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(expected_identifier, object_identifier);
  }

  std::unique_ptr<const Object> TryGetObject(const ObjectIdentifier& object_identifier,
                                             PageStorage::Location location,
                                             Status expected_status = Status::OK) {
    bool called;
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetObject(object_identifier, location,
                        callback::Capture(callback::SetWhenCalled(&called), &status, &object));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(expected_status, status);
    return object;
  }

  fsl::SizedVmo TryGetObjectPart(const ObjectIdentifier& object_identifier, size_t offset,
                                 size_t max_size, PageStorage::Location location,
                                 Status expected_status = Status::OK) {
    bool called;
    Status status;
    fsl::SizedVmo vmo;
    storage_->GetObjectPart(object_identifier, offset, max_size, location,
                            callback::Capture(callback::SetWhenCalled(&called), &status, &vmo));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(expected_status, status);
    return vmo;
  }

  std::unique_ptr<const Piece> TryGetPiece(const ObjectIdentifier& object_identifier,
                                           Status expected_status = Status::OK) {
    bool called;
    Status status;
    std::unique_ptr<const Piece> piece;
    storage_->GetPiece(object_identifier,
                       callback::Capture(callback::SetWhenCalled(&called), &status, &piece));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(expected_status, status);
    return piece;
  }

  std::vector<Entry> GetCommitContents(const Commit& commit) {
    bool called;
    Status status;
    std::vector<Entry> result;
    auto on_next = [&result](Entry e) {
      result.push_back(e);
      return true;
    };
    storage_->GetCommitContents(commit, "", std::move(on_next),
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

  Status WriteObject(CoroutineHandler* handler, ObjectData* data,
                     PageDbObjectStatus object_status = PageDbObjectStatus::TRANSIENT,
                     const ObjectReferencesAndPriority& references = {}) {
    return PageStorageImplAccessorForTest::GetDb(storage_).WriteObject(
        handler, DataChunkPiece(data->object_identifier, data->ToChunk()), object_status,
        references);
  }

  Status ReadObject(CoroutineHandler* handler, ObjectIdentifier object_identifier,
                    std::unique_ptr<const Piece>* piece) {
    return PageStorageImplAccessorForTest::GetDb(storage_).ReadObject(handler, object_identifier,
                                                                      piece);
  }

  // Checks that |object_identifier| is referenced by |expected_references|.
  void CheckInboundObjectReferences(CoroutineHandler* handler, ObjectIdentifier object_identifier,
                                    ObjectReferencesAndPriority expected_references) {
    ASSERT_FALSE(GetObjectDigestInfo(object_identifier.object_digest()).is_inlined())
        << "Broken test: CheckInboundObjectReferences must be called on "
           "non-inline pieces only.";
    ObjectReferencesAndPriority stored_references;
    ASSERT_EQ(Status::OK,
              PageStorageImplAccessorForTest::GetDb(storage_).GetInboundObjectReferences(
                  handler, object_identifier, &stored_references));
    EXPECT_THAT(stored_references, UnorderedElementsAreArray(expected_references));
  }

  // Checks that |object_identifier| is referenced by |expected_references|.
  void CheckInboundCommitReferences(CoroutineHandler* handler, ObjectIdentifier object_identifier,
                                    const std::vector<CommitId>& expected_references) {
    ASSERT_FALSE(GetObjectDigestInfo(object_identifier.object_digest()).is_inlined())
        << "Broken test: CheckInboundCommitReferences must be called on "
           "non-inline pieces only.";
    std::vector<CommitId> stored_references;
    ASSERT_EQ(Status::OK,
              PageStorageImplAccessorForTest::GetDb(storage_).GetInboundCommitReferences(
                  handler, object_identifier, &stored_references));
    EXPECT_THAT(stored_references, UnorderedElementsAreArray(expected_references));
  }

  ::testing::AssertionResult ObjectIsUntracked(ObjectIdentifier object_identifier,
                                               bool expected_untracked) {
    bool called;
    Status status;
    bool is_untracked;
    storage_->ObjectIsUntracked(
        object_identifier,
        callback::Capture(callback::SetWhenCalled(&called), &status, &is_untracked));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "ObjectIsUntracked for id " << object_identifier << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "ObjectIsUntracked for id " << object_identifier << " returned status " << status;
    }
    if (is_untracked != expected_untracked) {
      return ::testing::AssertionFailure()
             << "For id " << object_identifier << " expected to find the object "
             << (is_untracked ? "un" : "") << "tracked, but was "
             << (expected_untracked ? "un" : "") << "tracked, instead.";
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult IsPieceSynced(ObjectIdentifier object_identifier,
                                           bool expected_synced) {
    bool called;
    Status status;
    bool is_synced;
    storage_->IsPieceSynced(object_identifier, callback::Capture(callback::SetWhenCalled(&called),
                                                                 &status, &is_synced));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure()
             << "IsPieceSynced for id " << object_identifier << " didn't return.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "IsPieceSynced for id " << object_identifier << " returned status " << status;
    }
    if (is_synced != expected_synced) {
      return ::testing::AssertionFailure()
             << "For id " << object_identifier << " expected to find the object "
             << (is_synced ? "un" : "") << "synced, but was " << (expected_synced ? "un" : "")
             << "synced, instead.";
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult CreateNodeFromIdentifier(
      ObjectIdentifier identifier, std::unique_ptr<const btree::TreeNode>* node) {
    bool called;
    Status status;
    std::unique_ptr<const btree::TreeNode> result;
    btree::TreeNode::FromIdentifier(
        GetStorage(), std::move(identifier),
        callback::Capture(callback::SetWhenCalled(&called), &status, &result));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure() << "TreeNode::FromIdentifier callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure()
             << "TreeNode::FromIdentifier failed with status " << status;
    }
    node->swap(result);
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult CreateNodeFromEntries(
      const std::vector<Entry>& entries, const std::map<size_t, ObjectIdentifier>& children,
      std::unique_ptr<const btree::TreeNode>* node) {
    bool called;
    Status status;
    ObjectIdentifier identifier;
    btree::TreeNode::FromEntries(
        GetStorage(), 0u, entries, children,
        callback::Capture(callback::SetWhenCalled(&called), &status, &identifier));
    RunLoopUntilIdle();
    if (!called) {
      return ::testing::AssertionFailure() << "TreeNode::FromEntries callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "TreeNode::FromEntries failed with status " << status;
    }
    return CreateNodeFromIdentifier(identifier, node);
  }

  ::testing::AssertionResult GetEmptyNodeIdentifier(ObjectIdentifier* empty_node_identifier) {
    bool called;
    Status status;
    btree::TreeNode::Empty(GetStorage(), callback::Capture(callback::SetWhenCalled(&called),
                                                           &status, empty_node_identifier));
    RunLoopUntilIdle();
    if (!called) {
      return ::testing::AssertionFailure() << "TreeNode::Empty callback was not executed.";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "TreeNode::Empty failed with status " << status;
    }
    return ::testing::AssertionSuccess();
  }

  ControlledLevelDb* leveldb_;
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
  storage_->GetCommit(RandomCommitId(environment_.random()),
                      callback::Capture(callback::SetWhenCalled(&called), &status, &lookup_commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_NOT_FOUND, status);
  EXPECT_FALSE(lookup_commit);

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  // Search for a commit that exists and check the content.
  std::unique_ptr<const Commit> found = GetCommit(id);
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddLocalCommitsReferences) {
  // Create two commits pointing to the same non-inline object identifier by
  // creating two identical journals and commiting them. We then check that both
  // commits are stored as inbound references of said object.
  std::unique_ptr<const Commit> base = GetFirstHead();
  const ObjectData data(RandomString(environment_.random(), 65536), InlineBehavior::PREVENT);
  const ObjectIdentifier object_id = data.object_identifier;
  std::unique_ptr<Journal> journal = storage_->StartCommit(base->Clone());
  journal->Put("key", object_id, KeyPriority::EAGER);
  bool called;
  Status status;
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Advance the clock a bit.
  RunLoopFor(zx::sec(1));

  ObjectIdentifier root_node1 = commit1->GetRootIdentifier();

  journal = storage_->StartCommit(std::move(base));
  journal->Put("key", object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ObjectIdentifier root_node2 = commit2->GetRootIdentifier();

  CommitId id1 = commit1->GetId();
  CommitId id2 = commit2->GetId();
  EXPECT_NE(id1, id2);
  EXPECT_EQ(root_node1, root_node2);

  RunInCoroutine([this, root_node1, id1, id2](CoroutineHandler* handler) {
    CheckInboundCommitReferences(handler, root_node1, {id1, id2});
  });
}

TEST_F(PageStorageTest, AddCommitFromLocalDoNotMarkUnsynedAlreadySyncedCommit) {
  bool called;
  Status status;

  // Create a conflict.
  std::unique_ptr<const Commit> base = GetFirstHead();

  std::unique_ptr<Journal> journal = storage_->StartCommit(base->Clone());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  CommitId id1 = commit1->GetId();
  storage_->MarkCommitSynced(id1, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  journal = storage_->StartCommit(base->Clone());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  CommitId id2 = commit2->GetId();
  storage_->MarkCommitSynced(id2, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Make a merge commit. Merge commits only depend on their parents and
  // contents, so we can reproduce them.
  storage::ObjectIdentifier merged_object_id = RandomObjectIdentifier(environment_.random());
  journal = storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal->Put("key", merged_object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_merged1;
  storage_->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                &status, &commit_merged1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  CommitId merged_id1 = commit_merged1->GetId();

  auto commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(merged_id1, commits[0]->GetId());

  storage_->MarkCommitSynced(merged_id1,
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Add the commit again.
  journal = storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal->Put("key", merged_object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_merged2;
  storage_->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                &status, &commit_merged2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  CommitId merged_id2 = commit_merged2->GetId();

  // Check that the commit is not marked unsynced.
  commits = GetUnsyncedCommits();
  EXPECT_EQ(0u, commits.size());
}

TEST_F(PageStorageTest, AddCommitBeforeParentsError) {
  // Try to add a commit before its parent and see the error.
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random()));
  ObjectIdentifier empty_object_id;
  GetEmptyNodeIdentifier(&empty_object_id);
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      storage_->GetCommitTracker(), environment_.clock(), empty_object_id, std::move(parent));

  bool called;
  Status status;
  std::vector<CommitId> commit_ids;
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status, &commit_ids));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_NOT_FOUND, status);
}

TEST_F(PageStorageTest, AddCommitsOutOfOrderError) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit1 = CommitImpl::FromContentAndParents(
      storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));
  parent.clear();
  parent.push_back(commit1->Clone());
  auto commit2 = CommitImpl::FromContentAndParents(
      storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit2->GetId(), commit2->GetStorageBytes().ToString());
  commits_and_bytes.emplace_back(commit1->GetId(), commit1->GetStorageBytes().ToString());

  bool called;
  Status status;
  std::vector<CommitId> missing_ids;
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_NOT_FOUND, status);
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
        storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));
    CommitId id = commit->GetId();

    // Adding the commit should only request the tree node and the eager value.
    sync.object_requests.clear();
    bool called;
    Status status;
    std::vector<CommitId> missing_ids;
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(2u, sync.object_requests.size());
    EXPECT_TRUE(sync.object_requests.find(root_identifier) != sync.object_requests.end());
    EXPECT_TRUE(sync.object_requests.find(eager_value.object_identifier) !=
                sync.object_requests.end());

    // Adding the same commit twice should not request any objects from sync.
    sync.object_requests.clear();
    storage_->AddCommitsFromSync(
        CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
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

  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  CommitId id = commit->GetId();

  EXPECT_EQ(1u, GetUnsyncedCommits().size());

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
  std::vector<CommitId> missing_ids;
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(0u, GetUnsyncedCommits().size());
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();

  // Initially there should be no unsynced commits.
  EXPECT_TRUE(commits.empty());

  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  commits = GetUnsyncedCommits();
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(commit->GetStorageBytes(), commits[0]->GetStorageBytes());

  // Mark it as synced.
  storage_->MarkCommitSynced(commit->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  commits = GetUnsyncedCommits();
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, HeadCommits) {
  // Every page should have one initial head commit.
  std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
  EXPECT_EQ(1u, heads.size());

  // Adding a new commit with the previous head as its parent should replace the
  // old head.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  heads = GetHeads();
  ASSERT_EQ(1u, heads.size());
  EXPECT_EQ(commit->GetId(), heads[0]->GetId());
}

TEST_F(PageStorageTest, OrderHeadCommitsByTimestampThenId) {
  timekeeper::TestClock test_clock;
  // We generate a few timestamps: some random, and a few equal constants to
  // test ID ordering.
  std::vector<zx::time_utc> timestamps(7);
  std::generate(timestamps.begin(), timestamps.end(),
                [this] { return environment_.random()->Draw<zx::time_utc>(); });
  timestamps.insert(timestamps.end(), {zx::time_utc(1000), zx::time_utc(1000), zx::time_utc(1000)});
  std::shuffle(timestamps.begin(), timestamps.end(),
               environment_.random()->NewBitGenerator<size_t>());

  std::vector<ObjectIdentifier> object_identifiers;
  object_identifiers.resize(timestamps.size());
  for (size_t i = 0; i < timestamps.size(); ++i) {
    ObjectData value("value" + std::to_string(i), InlineBehavior::ALLOW);
    std::vector<Entry> entries = {
        Entry{"key" + std::to_string(i), value.object_identifier, KeyPriority::EAGER}};
    std::unique_ptr<const btree::TreeNode> node;
    ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
    object_identifiers[i] = node->GetIdentifier();
  }

  std::unique_ptr<const Commit> base = GetFirstHead();

  // We first generate the commits. The will be shuffled at a later time.
  std::vector<PageStorage::CommitIdAndBytes> commits;
  std::vector<std::pair<zx::time_utc, CommitId>> sorted_commits;
  for (size_t i = 0; i < timestamps.size(); i++) {
    test_clock.Set(timestamps[i]);
    std::vector<std::unique_ptr<const Commit>> parent;
    parent.push_back(base->Clone());
    std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
        storage_->GetCommitTracker(), &test_clock, object_identifiers[i], std::move(parent));

    commits.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
    sorted_commits.emplace_back(timestamps[i], commit->GetId());
  }

  auto rng = environment_.random()->NewBitGenerator<uint64_t>();
  std::shuffle(commits.begin(), commits.end(), rng);
  bool called;
  Status status;
  std::vector<CommitId> missing_ids;
  storage_->AddCommitsFromSync(
      std::move(commits), ChangeSource::CLOUD,
      callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Check that GetHeadCommitIds returns sorted commits.
  std::vector<std::unique_ptr<const Commit>> heads;
  status = storage_->GetHeadCommits(&heads);
  EXPECT_EQ(Status::OK, status);
  std::sort(sorted_commits.begin(), sorted_commits.end());
  ASSERT_EQ(sorted_commits.size(), heads.size());
  for (size_t i = 0; i < sorted_commits.size(); ++i) {
    EXPECT_EQ(sorted_commits[i].second, heads[i]->GetId());
  }
}

TEST_F(PageStorageTest, CreateJournals) {
  // Explicit journal.
  auto left_commit = TryCommitFromLocal(5);
  ASSERT_TRUE(left_commit);
  auto right_commit = TryCommitFromLocal(10);
  ASSERT_TRUE(right_commit);

  // Journal for merge commit.
  std::unique_ptr<Journal> journal =
      storage_->StartMergeCommit(std::move(left_commit), std::move(right_commit));
}

TEST_F(PageStorageTest, CreateJournalHugeNode) {
  std::unique_ptr<const Commit> commit = TryCommitFromLocal(500, 1024);
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
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  bool found_index = false;
  std::set<ObjectIdentifier> unsynced_identifiers(object_identifiers.begin(),
                                                  object_identifiers.end());
  for (const auto& identifier : unsynced_identifiers) {
    EXPECT_FALSE(GetObjectDigestInfo(identifier.object_digest()).is_inlined());

    if (GetObjectDigestInfo(identifier.object_digest()).piece_type == PieceType::INDEX) {
      found_index = true;
      std::set<ObjectIdentifier> sub_identifiers;
      IterationStatus iteration_status = IterationStatus::ERROR;
      CollectPieces(
          identifier,
          [this](ObjectIdentifier identifier,
                 fit::function<void(Status, fxl::StringView)> callback) {
            storage_->GetPiece(std::move(identifier),
                               [callback = std::move(callback)](
                                   Status status, std::unique_ptr<const Piece> piece) {
                                 if (status != Status::OK) {
                                   callback(status, "");
                                   return;
                                 }
                                 callback(status, piece->GetData());
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

TEST_F(PageStorageTest, DestroyUncommittedJournal) {
  // It is not an error if a journal is not committed or rolled back.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(data.object_identifier, object_identifier);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(Status::OK, ReadObject(handler, object_identifier, &piece));
    EXPECT_EQ(data.value, piece->GetData());
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
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(data.object_identifier, object_identifier);
    EXPECT_EQ(data.value, ExtractObjectDigestData(object_identifier.object_digest()));

    std::unique_ptr<const Piece> piece;
    EXPECT_EQ(Status::INTERNAL_NOT_FOUND, ReadObject(handler, object_identifier, &piece));
    // Inline objects do not need to ever be tracked.
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, false));
  });
}

TEST_F(PageStorageTest, InterruptAddObjectFromLocal) {
  ObjectData data("Some data");

  storage_->AddObjectFromLocal(ObjectType::BLOB, data.ToDataSource(), {},
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
      ObjectType::BLOB, std::move(data_source), {},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::IO_ERROR, status);
}

TEST_F(PageStorageTest, AddLocalPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);
    const ObjectIdentifier reference = RandomObjectIdentifier(environment_.random());

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::LOCAL, IsObjectSynced::NO,
        {{reference.object_digest(), KeyPriority::LAZY}},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &piece));
    EXPECT_EQ(data.value, piece->GetData());
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));

    CheckInboundObjectReferences(handler, reference,
                                 {{data.object_identifier.object_digest(), KeyPriority::LAZY}});
  });
}

TEST_F(PageStorageTest, AddSyncPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);
    const ObjectIdentifier reference = RandomObjectIdentifier(environment_.random());

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::CLOUD, IsObjectSynced::YES,
        {{reference.object_digest(), KeyPriority::EAGER}},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &piece));
    EXPECT_EQ(data.value, piece->GetData());
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, true));

    CheckInboundObjectReferences(handler, reference,
                                 {{data.object_identifier.object_digest(), KeyPriority::EAGER}});
  });
}

TEST_F(PageStorageTest, AddP2PPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::P2P, IsObjectSynced::NO, {},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(Status::OK, ReadObject(handler, data.object_identifier, &piece));
    EXPECT_EQ(data.value, piece->GetData());
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

TEST_F(PageStorageTest, GetObjectPart) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("_Some data_");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    fsl::SizedVmo object_part =
        TryGetObjectPart(data.object_identifier, 1, data.size - 2, PageStorage::Location::LOCAL);
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(data.value.substr(1, data.size - 2), convert::ToString(object_part_data));
  });
}

TEST_F(PageStorageTest, GetObjectPartLargeOffset) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("_Some data_");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    fsl::SizedVmo object_part = TryGetObjectPart(data.object_identifier, data.size * 2, data.size,
                                                 PageStorage::Location::LOCAL);
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ("", convert::ToString(object_part_data));
  });
}

TEST_F(PageStorageTest, GetObjectPartLargeMaxSize) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("_Some data_");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    fsl::SizedVmo object_part =
        TryGetObjectPart(data.object_identifier, 0, data.size * 2, PageStorage::Location::LOCAL);
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(data.value, convert::ToString(object_part_data));
  });
}

TEST_F(PageStorageTest, GetObjectPartNegativeArgs) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data("_Some data_");
    ASSERT_EQ(Status::OK, WriteObject(handler, &data));

    fsl::SizedVmo object_part =
        TryGetObjectPart(data.object_identifier, -data.size + 1, -1, PageStorage::Location::LOCAL);
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(data.value.substr(1, data.size - 1), convert::ToString(object_part_data));
  });
}

TEST_F(PageStorageTest, GetLargeObjectPart) {
  std::string data_str = RandomString(environment_.random(), 65536);
  size_t offset = 6144;
  size_t size = 49152;

  ObjectData data(std::move(data_str), InlineBehavior::PREVENT);

  ASSERT_EQ(PieceType::INDEX,
            GetObjectDigestInfo(data.object_identifier.object_digest()).piece_type);

  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      ObjectType::BLOB, data.ToDataSource(), /*tree_references=*/{},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(data.object_identifier, object_identifier);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::LOCAL);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  std::string result_str = convert::ToString(object_part_data);
  EXPECT_EQ(size, result_str.size());
  EXPECT_EQ(data.value.substr(offset, size), result_str);
}

TEST_F(PageStorageTest, GetObjectPartFromSync) {
  ObjectData data("_Some data_", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data.value);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part =
      TryGetObjectPart(data.object_identifier, 1, data.size - 2, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data.value.substr(1, data.size - 2), convert::ToString(object_part_data));

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data("_Some other data_", InlineBehavior::PREVENT);
  TryGetObjectPart(other_data.object_identifier, 1, other_data.size - 2,
                   PageStorage::Location::LOCAL, Status::INTERNAL_NOT_FOUND);
  TryGetObjectPart(other_data.object_identifier, 1, other_data.size - 2,
                   PageStorage::Location::NETWORK, Status::NETWORK_ERROR);
}

TEST_F(PageStorageTest, GetObjectPartFromSyncEndOfChunk) {
  // Test for LE-797: GetObjectPartFromSync was sometimes called to read zero
  // bytes off a piece.
  // Generates a read such that the end of the read is on a boundary between two
  // chunks.

  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);

  FakeSyncDelegate sync;
  // Given the length of the piece, there will be at least two non-inlined
  // chunks. This relies on ForEachPiece giving the chunks in order.
  std::vector<size_t> chunk_lengths;
  std::vector<ObjectIdentifier> chunk_identifiers;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::BLOB,
      [&sync, &chunk_lengths, &chunk_identifiers](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        ObjectDigestInfo digest_info = GetObjectDigestInfo(object_identifier.object_digest());
        if (digest_info.is_chunk()) {
          chunk_lengths.push_back(piece->GetData().size());
          chunk_identifiers.push_back(object_identifier);
        }
        if (digest_info.is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  // Read 128 bytes off the end of the first chunk.
  uint64_t size = 128;
  ASSERT_LT(size, chunk_lengths[0]);
  uint64_t offset = chunk_lengths[0] - size;

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data_str.substr(offset, size), convert::ToString(object_part_data));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(object_identifier));
  EXPECT_THAT(sync.object_requests, Contains(chunk_identifiers[0]));
  EXPECT_THAT(sync.object_requests, Not(Contains(chunk_identifiers[1])));
}

TEST_F(PageStorageTest, GetObjectPartFromSyncStartOfChunk) {
  // Generates a read such that the start of the read is on a boundary between
  // two chunks.

  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);

  FakeSyncDelegate sync;
  // Given the length of the piece, there will be at least two non-inlined
  // chunks. This relies on ForEachPiece giving the chunks in order.
  std::vector<size_t> chunk_lengths;
  std::vector<ObjectIdentifier> chunk_identifiers;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::BLOB,
      [&sync, &chunk_lengths, &chunk_identifiers](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        ObjectDigestInfo digest_info = GetObjectDigestInfo(object_identifier.object_digest());
        if (digest_info.is_chunk()) {
          chunk_lengths.push_back(piece->GetData().size());
          chunk_identifiers.push_back(object_identifier);
        }
        if (digest_info.is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  // Read 128 bytes off the start of the second chunk.
  uint64_t size = 128;
  ASSERT_LT(size, chunk_lengths[1]);
  uint64_t offset = chunk_lengths[0];

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data_str.substr(offset, size), convert::ToString(object_part_data));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(object_identifier));
  EXPECT_THAT(sync.object_requests, Not(Contains(chunk_identifiers[0])));
  EXPECT_THAT(sync.object_requests, Contains(chunk_identifiers[1]));
}

TEST_F(PageStorageTest, GetObjectPartFromSyncZeroBytes) {
  // Generates a read that falls inside a chunk but reads zero bytes.
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);

  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        ObjectDigestInfo digest_info = GetObjectDigestInfo(object_identifier.object_digest());
        if (digest_info.is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  // Read zero bytes inside a chunk. This succeeds and only reads the root
  // piece.
  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, 12, 0, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ("", convert::ToString(object_part_data));
  EXPECT_THAT(sync.object_requests, ElementsAre(object_identifier));
}

TEST_F(PageStorageTest, GetObjectPartFromSyncZeroBytesNotFound) {
  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);

  // Reading zero bytes from non-existing objects returns an error.
  ObjectData other_data("_Some other data_", InlineBehavior::PREVENT);
  TryGetObjectPart(other_data.object_identifier, 1, 0, PageStorage::Location::NETWORK,
                   Status::INTERNAL_NOT_FOUND);
}

TEST_F(PageStorageTest, GetHugeObjectPartFromSync) {
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);
  int64_t offset = 28672;
  int64_t size = 128;

  FakeSyncDelegate sync;
  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB,
                   [&sync, &digest_to_identifier](std::unique_ptr<const Piece> piece) {
                     ObjectIdentifier object_identifier = piece->GetIdentifier();
                     if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
                       return;
                     }
                     digest_to_identifier[object_identifier.object_digest()] = object_identifier;
                     sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
                   });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data_str.substr(offset, size), convert::ToString(object_part_data));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(object_identifier));
  // Check that the requested pieces have been added to storage, and collect
  // their outbound references into an inbound-references map. Note that we need
  // to collect references only from piece actually added to storage, rather
  // than all pieces from |ForEachPiece|, since pieces not present in storage do
  // not contribute to reference counting.
  std::map<ObjectIdentifier, ObjectReferencesAndPriority> inbound_references;
  for (const ObjectIdentifier& piece_identifier : sync.object_requests) {
    auto piece = TryGetPiece(piece_identifier);
    ASSERT_NE(piece, nullptr);
    ObjectReferencesAndPriority outbound_references;
    ASSERT_EQ(piece->AppendReferences(&outbound_references), Status::OK);
    for (const auto& [reference, priority] : outbound_references) {
      auto reference_identifier = digest_to_identifier.find(reference);
      ASSERT_NE(reference_identifier, digest_to_identifier.end());
      inbound_references[reference_identifier->second].emplace(piece_identifier.object_digest(),
                                                               priority);
    }
  }
  // Check that references have been stored correctly.
  RunInCoroutine(
      [this, inbound_references = std::move(inbound_references)](CoroutineHandler* handler) {
        for (const auto& [identifier, references] : inbound_references) {
          CheckInboundObjectReferences(handler, identifier, references);
        }
      });
}

TEST_F(PageStorageTest, GetHugeObjectPartFromSyncNegativeOffset) {
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);
  int64_t offset = -28672;
  int64_t size = 128;

  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data_str.substr(data_str.size() + offset, size), convert::ToString(object_part_data));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  // Check that at least the root piece has been added to storage.
  TryGetPiece(object_identifier);
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
  // Check that the piece has been added to storage (it is small enough that
  // there is only one piece).
  TryGetPiece(data.object_identifier);

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data("Some other data", InlineBehavior::PREVENT);
  TryGetObject(other_data.object_identifier, PageStorage::Location::LOCAL,
               Status::INTERNAL_NOT_FOUND);
  TryGetObject(other_data.object_identifier, PageStorage::Location::NETWORK, Status::NETWORK_ERROR);
}

TEST_F(PageStorageTest, FullDownloadAfterPartial) {
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);
  int64_t offset = 0;
  int64_t size = 128;

  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::NETWORK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(data_str.substr(offset, size), convert::ToString(object_part_data));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  TryGetObject(object_identifier, PageStorage::LOCAL, Status::INTERNAL_NOT_FOUND);
  // Check that all requested pieces have been stored locally.
  for (const auto& piece_identifier : sync.object_requests) {
    TryGetPiece(piece_identifier);
  }

  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::NETWORK);
  fxl::StringView object_data;
  ASSERT_EQ(Status::OK, object->GetData(&object_data));
  EXPECT_EQ(data_str, convert::ToString(object_data));
  EXPECT_EQ(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  TryGetObject(object_identifier, PageStorage::LOCAL, Status::OK);
  // Check that all pieces have been stored locally.
  for (const auto& piece_identifier : sync.object_requests) {
    TryGetPiece(piece_identifier);
  }
}

TEST_F(PageStorageTest, GetObjectFromSyncWrongId) {
  ObjectData data("Some data", InlineBehavior::PREVENT);
  ObjectData data2("Some data2", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data2.value);
  storage_->SetSyncDelegate(&sync);

  TryGetObject(data.object_identifier, PageStorage::Location::NETWORK,
               Status::DATA_INTEGRITY_ERROR);
}

TEST_F(PageStorageTest, AddAndGetHugeTreenodeFromLocal) {
  std::string data_str = RandomString(environment_.random(), 65536);

  ObjectData data(std::move(data_str), ObjectType::TREE_NODE, InlineBehavior::PREVENT);
  // An identifier to another tree node pointed at by the current one.
  const ObjectIdentifier tree_reference = RandomObjectIdentifier(environment_.random());
  ASSERT_EQ(ObjectType::TREE_NODE,
            GetObjectDigestInfo(data.object_identifier.object_digest()).object_type);
  ASSERT_EQ(PieceType::INDEX,
            GetObjectDigestInfo(data.object_identifier.object_digest()).piece_type);
  ASSERT_EQ(InlinedPiece::NO, GetObjectDigestInfo(data.object_identifier.object_digest()).inlined);

  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      ObjectType::TREE_NODE, data.ToDataSource(),
      {{tree_reference.object_digest(), KeyPriority::LAZY}},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(Status::OK, status);
  // This ensures that the object is encoded with an index, as we checked the
  // piece type of |data.object_identifier| above.
  EXPECT_EQ(data.object_identifier, object_identifier);

  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::LOCAL);
  fxl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data.value, content);
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
  EXPECT_TRUE(IsPieceSynced(object_identifier, false));

  // Check that the index piece obtained at |object_identifier| is different
  // from the object itself, ie. that some splitting occurred.
  std::unique_ptr<const Piece> piece = TryGetPiece(object_identifier);
  ASSERT_NE(piece, nullptr);
  EXPECT_NE(content, piece->GetData());

  RunInCoroutine([this, piece = std::move(piece), tree_reference,
                  object_identifier](CoroutineHandler* handler) {
    // Check tree reference.
    CheckInboundObjectReferences(handler, tree_reference,
                                 {{object_identifier.object_digest(), KeyPriority::LAZY}});
    // Check piece references.
    ASSERT_EQ(
        Status::OK, ForEachIndexChild(piece->GetData(), [this, handler, object_identifier](
                                                            ObjectIdentifier piece_identifier) {
          if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
            // References to inline pieces are not stored on disk.
            return Status::OK;
          }
          CheckInboundObjectReferences(handler, piece_identifier,
                                       {{object_identifier.object_digest(), KeyPriority::EAGER}});
          return Status::OK;
        }));
  });
}

TEST_F(PageStorageTest, AddAndGetHugeTreenodeFromSync) {
  // Build a random, valid tree node.
  std::vector<Entry> entries;
  std::map<size_t, ObjectIdentifier> children;
  for (size_t i = 0; i < 1000; ++i) {
    entries.push_back(Entry{RandomString(environment_.random(), 50),
                            RandomObjectIdentifier(environment_.random()),
                            i % 2 ? KeyPriority::EAGER : KeyPriority::LAZY});
    children.emplace(i, RandomObjectIdentifier(environment_.random()));
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry& e1, const Entry& e2) { return e1.key < e2.key; });
  std::string data_str = btree::EncodeNode(0, entries, children);
  ASSERT_TRUE(btree::CheckValidTreeNodeSerialization(data_str));

  // Split the tree node content into pieces, add them to a SyncDelegate to be
  // retrieved by GetObject, and store inbound piece references into a map to
  // check them later.
  FakeSyncDelegate sync;
  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  std::map<ObjectIdentifier, ObjectReferencesAndPriority> inbound_references;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::TREE_NODE,
      [&sync, &digest_to_identifier, &inbound_references](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier piece_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
          return;
        }
        digest_to_identifier[piece_identifier.object_digest()] = piece_identifier;
        ObjectReferencesAndPriority outbound_references;
        ASSERT_EQ(piece->AppendReferences(&outbound_references), Status::OK);
        for (const auto& [reference, priority] : outbound_references) {
          auto reference_identifier = digest_to_identifier.find(reference);
          // ForEachPiece returns pieces in order, so we must have already seen
          // pieces referenced by the current one.
          ASSERT_NE(reference_identifier, digest_to_identifier.end());
          inbound_references[reference_identifier->second].emplace(piece_identifier.object_digest(),
                                                                   priority);
        }
        sync.AddObject(std::move(piece_identifier), piece->GetData().ToString());
      });
  ASSERT_EQ(PieceType::INDEX, GetObjectDigestInfo(object_identifier.object_digest()).piece_type);
  storage_->SetSyncDelegate(&sync);

  // Add object references to the inbound references map.
  for (const Entry& entry : entries) {
    inbound_references[entry.object_identifier].emplace(object_identifier.object_digest(),
                                                        entry.priority);
  }
  for (const auto& [size, child_identifier] : children) {
    inbound_references[child_identifier].emplace(object_identifier.object_digest(),
                                                 KeyPriority::EAGER);
  }

  // Get the object from network and check that it is correct.
  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::NETWORK);
  fxl::StringView content;
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data_str, content);

  // Check that all pieces have been stored locally.
  EXPECT_EQ(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  for (const auto& piece_identifier : sync.object_requests) {
    TryGetPiece(piece_identifier);
  }

  // Check that references have been stored correctly.
  RunInCoroutine(
      [this, inbound_references = std::move(inbound_references)](CoroutineHandler* handler) {
        for (const auto& [identifier, references] : inbound_references) {
          CheckInboundObjectReferences(handler, identifier, references);
        }
      });

  // Now that the object has been retrieved from network, we should be able to
  // retrieve it again locally.
  auto local_object = TryGetObject(object_identifier, PageStorage::LOCAL, Status::OK);
  ASSERT_EQ(Status::OK, object->GetData(&content));
  EXPECT_EQ(data_str, content);
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
    std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());

    journal->Put(fxl::StringPrintf("key%lu", i), data_array[i].object_identifier,
                 KeyPriority::LAZY);
    EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
    commits.push_back(GetFirstHead()->GetId());
  }

  // GetUnsyncedPieces should return the ids of all objects: 3 values and
  // the 3 root nodes of the 3 commits.
  bool called;
  Status status;
  std::vector<ObjectIdentifier> object_identifiers;
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(6u, object_identifiers.size());
  for (size_t i = 0; i < size; ++i) {
    std::unique_ptr<const Commit> commit = GetCommit(commits[i]);
    EXPECT_TRUE(std::find_if(object_identifiers.begin(), object_identifiers.end(),
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
  storage_->MarkPieceSynced(data_array[1].object_identifier,
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
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), commit->GetRootIdentifier()) !=
              objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), data_array[0].object_identifier) !=
              objects.end());
  EXPECT_TRUE(std::find(objects.begin(), objects.end(), data_array[2].object_identifier) !=
              objects.end());
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
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(true, is_synced);

  // Add all objects in one commit.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  for (size_t i = 0; i < size; ++i) {
    journal->Put(fxl::StringPrintf("key%lu", i), data_array[i].object_identifier,
                 KeyPriority::LAZY);
  }
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  CommitId commit_id = GetFirstHead()->GetId();

  // After commiting, the page is unsynced.
  called = false;
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_synced);
  // Mark objects (and the root tree node) as synced and expect that the page is
  // still unsynced.
  for (const auto& data : data_array) {
    called = false;
    storage_->MarkPieceSynced(data.object_identifier,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }

  called = false;
  storage_->MarkPieceSynced(GetFirstHead()->GetRootIdentifier(),
                            callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  called = false;
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_synced);

  // Mark the commit as synced and expect that the page is synced.
  called = false;
  storage_->MarkCommitSynced(commit_id,
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  called = false;
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
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
  std::unique_ptr<const Commit> commit = TryCommitFromLocal(size);
  EXPECT_FALSE(storage_->IsOnline());

  // Mark all objects as synced. The page is still not online: other devices
  // will only see these objects if the corresponding commit is also synced to
  // the cloud.
  bool called;
  Status status;
  std::vector<ObjectIdentifier> object_identifiers;
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  for (ObjectIdentifier& object_identifier : object_identifiers) {
    storage_->MarkPieceSynced(object_identifier,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
  }
  EXPECT_FALSE(storage_->IsOnline());

  // Mark the commit as synced. The page should now be marked as online.
  storage_->MarkCommitSynced(commit->GetId(),
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
  storage_->MarkSyncedToPeer(callback::Capture(callback::SetWhenCalled(&called), &status));
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
  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(is_empty);

  // Add an entry and expect that the page is not empty any more.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", value.object_identifier, KeyPriority::LAZY);
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(is_empty);

  // Clear the page and expect it to be empty again.
  journal = storage_->StartCommit(GetFirstHead());
  journal->Delete("key");
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
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
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", data.object_identifier, KeyPriority::EAGER);
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
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key0", data_array[0].object_identifier, KeyPriority::LAZY);
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, true));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, false));
  EXPECT_TRUE(ObjectIsUntracked(data_array[1].object_identifier, true));
  EXPECT_TRUE(ObjectIsUntracked(data_array[2].object_identifier, true));

  // Create a second commit. After calling Put for "key1" for the second time
  // data_array[1] is no longer part of this commit: it should remain
  // untracked after committing.
  journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key1", data_array[1].object_identifier, KeyPriority::LAZY);
  journal->Put("key2", data_array[2].object_identifier, KeyPriority::LAZY);
  journal->Put("key1", data_array[2].object_identifier, KeyPriority::LAZY);
  journal->Put("key3", data_array[0].object_identifier, KeyPriority::LAZY);
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(data_array[0].object_identifier, false));
  EXPECT_TRUE(ObjectIsUntracked(data_array[1].object_identifier, true));
  EXPECT_TRUE(ObjectIsUntracked(data_array[2].object_identifier, false));
}

TEST_F(PageStorageTest, CommitWatchers) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  // Add a watcher and receive the commit.
  auto expected = TryCommitFromLocal(10);
  ASSERT_TRUE(expected);
  EXPECT_EQ(1, watcher.commit_count);
  EXPECT_EQ(expected->GetId(), watcher.last_commit_id);
  EXPECT_EQ(ChangeSource::LOCAL, watcher.last_source);

  // Add a second watcher.
  FakeCommitWatcher watcher2;
  storage_->AddCommitWatcher(&watcher2);
  expected = TryCommitFromLocal(10);
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

// If a commit fails to be persisted on disk, no notification should be sent.
TEST_F(PageStorageTest, CommitFailNoWatchNotification) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);
  EXPECT_EQ(0, watcher.commit_count);

  // Create the commit.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key1", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);

  leveldb_->SetFailBatchExecuteAfter(1);
  std::unique_ptr<const Commit> commit = TryCommitJournal(std::move(journal), Status::IO_ERROR);

  // The watcher is not called.
  EXPECT_EQ(0, watcher.commit_count);
}

TEST_F(PageStorageTest, SyncMetadata) {
  std::vector<std::pair<fxl::StringView, fxl::StringView>> keys_and_values = {{"foo1", "foo2"},
                                                                              {"bar1", " bar2 "}};
  for (const auto& key_and_value : keys_and_values) {
    auto key = key_and_value.first;
    auto value = key_and_value.second;
    bool called;
    Status status;
    std::string returned_value;
    storage_->GetSyncMetadata(
        key, callback::Capture(callback::SetWhenCalled(&called), &status, &returned_value));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::INTERNAL_NOT_FOUND, status);

    storage_->SetSyncMetadata(key, value,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    storage_->GetSyncMetadata(
        key, callback::Capture(callback::SetWhenCalled(&called), &status, &returned_value));
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
      std::vector<Entry> entries = {
          Entry{"key" + std::to_string(i), value.object_identifier, KeyPriority::EAGER}};
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
    std::unique_ptr<const Commit> commit0 =
        CommitImpl::FromContentAndParents(storage_->GetCommitTracker(), environment_.clock(),
                                          object_identifiers[0], std::move(parent));
    parent.clear();

    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit1 =
        CommitImpl::FromContentAndParents(storage_->GetCommitTracker(), environment_.clock(),
                                          object_identifiers[1], std::move(parent));
    parent.clear();

    parent.emplace_back(commit1->Clone());
    std::unique_ptr<const Commit> commit2 =
        CommitImpl::FromContentAndParents(storage_->GetCommitTracker(), environment_.clock(),
                                          object_identifiers[2], std::move(parent));

    std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
    commits_and_bytes.emplace_back(commit0->GetId(), commit0->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit1->GetId(), commit1->GetStorageBytes().ToString());
    commits_and_bytes.emplace_back(commit2->GetId(), commit2->GetStorageBytes().ToString());

    bool called;
    Status status;
    std::vector<CommitId> missing_ids;
    storage_->AddCommitsFromSync(
        std::move(commits_and_bytes), ChangeSource::CLOUD,
        callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);

    EXPECT_EQ(4u, sync.object_requests.size());
    EXPECT_NE(sync.object_requests.find(object_identifiers[0]), sync.object_requests.end());
    EXPECT_EQ(sync.object_requests.find(object_identifiers[1]), sync.object_requests.end());
    EXPECT_NE(sync.object_requests.find(object_identifiers[2]), sync.object_requests.end());
  });
}

TEST_F(PageStorageTest, Generation) {
  std::unique_ptr<const Commit> commit1 = TryCommitFromLocal(3);
  ASSERT_TRUE(commit1);
  EXPECT_EQ(1u, commit1->GetGeneration());

  std::unique_ptr<const Commit> commit2 = TryCommitFromLocal(3);
  ASSERT_TRUE(commit2);
  EXPECT_EQ(2u, commit2->GetGeneration());

  std::unique_ptr<Journal> journal =
      storage_->StartMergeCommit(std::move(commit1), std::move(commit2));

  std::unique_ptr<const Commit> commit3 = TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(commit3);
  EXPECT_EQ(3u, commit3->GetGeneration());
}

TEST_F(PageStorageTest, GetEntryFromCommit) {
  int size = 10;
  std::unique_ptr<const Commit> commit = TryCommitFromLocal(size);
  ASSERT_TRUE(commit);

  bool called;
  Status status;
  Entry entry;
  storage_->GetEntryFromCommit(
      *commit, "key not found",
      callback::Capture(callback::SetWhenCalled(&called), &status, &entry));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::KEY_NOT_FOUND, status);

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
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  journal = storage_->StartCommit(std::move(commit1));
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  EXPECT_EQ(2, watcher.commit_count);
  EXPECT_EQ(commit2->GetId(), watcher.last_commit_id);
}

TEST_F(PageStorageTest, NoOpCommit) {
  std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
  ASSERT_FALSE(heads.empty());

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());

  // Create a key, and delete it.
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  journal->Delete("key");

  // Commit the journal.
  bool called;
  Status status;
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  // Commiting a no-op commit should result in a successful status, but a null
  // commit.
  ASSERT_EQ(Status::OK, status);
  ASSERT_FALSE(commit);
}

// Check that receiving a remote commit and commiting the same commit locally at
// the same time do not prevent the commit to be marked as unsynced.
TEST_F(PageStorageTest, MarkRemoteCommitSyncedRace) {
  // We need a commit that we can add both "from sync" and locally. For this
  // purpose, we use a merge commit: we create a conflict, then a merge. We
  // propagate the conflicting commits through synchronization, and then both
  // add the merge and create it locally.
  bool called;
  Status status;
  std::unique_ptr<const Commit> base_commit = GetFirstHead();
  ObjectData value_1("data1", InlineBehavior::ALLOW);
  ObjectData value_2("data2", InlineBehavior::ALLOW);
  ObjectData value_3("data3", InlineBehavior::ALLOW);

  std::unique_ptr<Journal> journal1 = storage_->StartCommit(base_commit->Clone());
  journal1->Put("key", value_1.object_identifier, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal1),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  RunLoopFor(zx::sec(1));

  std::unique_ptr<Journal> journal2 = storage_->StartCommit(base_commit->Clone());
  journal2->Put("key", value_2.object_identifier, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal2),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Create a merge.
  std::unique_ptr<Journal> journal3 =
      storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal3->Put("key", value_3.object_identifier, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit3;
  storage_->CommitJournal(std::move(journal3),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit3));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  CommitId id3 = commit3->GetId();
  std::map<ObjectIdentifier, std::string> object_data_base;
  object_data_base[commit1->GetRootIdentifier()] =
      TryGetPiece(commit1->GetRootIdentifier())->GetData().ToString();
  object_data_base[commit2->GetRootIdentifier()] =
      TryGetPiece(commit2->GetRootIdentifier())->GetData().ToString();
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes_base;
  commits_and_bytes_base.emplace_back(commit1->GetId(), commit1->GetStorageBytes().ToString());
  commits_and_bytes_base.emplace_back(commit2->GetId(), commit2->GetStorageBytes().ToString());

  std::map<ObjectIdentifier, std::string> object_data_merge;
  object_data_merge[commit3->GetRootIdentifier()] =
      TryGetPiece(commit3->GetRootIdentifier())->GetData().ToString();
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes_merge;
  commits_and_bytes_merge.emplace_back(commit3->GetId(), commit3->GetStorageBytes().ToString());

  // We have extracted the commit and object data. We now reset the state of
  // PageStorage so we can add them again (in a controlled manner).
  base_commit.reset();
  commit1.reset();
  commit2.reset();
  commit3.reset();
  ResetStorage();

  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);
  for (const auto& data : object_data_base) {
    sync.AddObject(data.first, data.second);
  }

  // Start adding the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  std::vector<CommitId> missing_ids;
  storage_->AddCommitsFromSync(std::move(commits_and_bytes_base), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status, &missing_ids));
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(Status::OK, commits_from_sync_status);
  EXPECT_EQ(0u, missing_ids.size());
  ASSERT_EQ(2u, GetHeads().size());

  bool sync_delegate_called;
  fit::closure sync_delegate_call;
  DelayingFakeSyncDelegate sync2(
      callback::Capture(callback::SetWhenCalled(&sync_delegate_called), &sync_delegate_call));
  storage_->SetSyncDelegate(&sync2);

  for (const auto& data : object_data_merge) {
    sync2.AddObject(data.first, data.second);
  }

  storage_->AddCommitsFromSync(std::move(commits_and_bytes_merge), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status, &missing_ids));

  // Make the loop run until GetObject is called in sync, and before
  // AddCommitsFromSync finishes.
  RunLoopUntilIdle();
  EXPECT_TRUE(sync_delegate_called);
  EXPECT_FALSE(commits_from_sync_called);

  // Add the local commit.
  auto heads = GetHeads();
  Status commits_from_local_status;
  std::unique_ptr<Journal> journal =
      storage_->StartMergeCommit(std::move(heads[0]), std::move(heads[1]));
  journal->Put("key", value_3.object_identifier, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(
      std::move(journal),
      callback::Capture(callback::SetWhenCalled(&called), &commits_from_local_status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, commits_from_local_status);
  EXPECT_FALSE(commits_from_sync_called);

  EXPECT_EQ(id3, commit->GetId());

  // The local commit should be commited.
  ASSERT_TRUE(sync_delegate_call);
  sync_delegate_call();

  // Let the two AddCommit finish.
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(Status::OK, commits_from_sync_status);
  EXPECT_EQ(Status::OK, commits_from_local_status);

  // Verify that the commit is added correctly.
  storage_->GetCommit(id3, callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
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
  std::unique_ptr<const Commit> root = GetFirstHead();
  std::unique_ptr<Journal> journal_a = storage_->StartCommit(root->Clone());
  journal_a->Put("a", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_a = TryCommitJournal(std::move(journal_a), Status::OK);
  ASSERT_TRUE(commit_a);
  EXPECT_EQ(1u, commit_a->GetGeneration());

  std::unique_ptr<Journal> journal_b = storage_->StartCommit(root->Clone());
  journal_b->Put("b", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_b = TryCommitJournal(std::move(journal_b), Status::OK);
  ASSERT_TRUE(commit_b);
  EXPECT_EQ(1u, commit_b->GetGeneration());

  std::unique_ptr<Journal> journal_merge =
      storage_->StartMergeCommit(std::move(commit_a), std::move(commit_b));

  std::unique_ptr<const Commit> commit_merge =
      TryCommitJournal(std::move(journal_merge), Status::OK);
  ASSERT_TRUE(commit_merge);
  EXPECT_EQ(2u, commit_merge->GetGeneration());

  std::unique_ptr<Journal> journal_c = storage_->StartCommit(std::move(root));
  journal_c->Put("c", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_c = TryCommitJournal(std::move(journal_c), Status::OK);
  ASSERT_TRUE(commit_c);
  EXPECT_EQ(1u, commit_c->GetGeneration());

  // Verify that the merge commit is returned as last, even though commit C is
  // older.
  std::vector<std::unique_ptr<const Commit>> unsynced_commits = GetUnsyncedCommits();
  EXPECT_EQ(4u, unsynced_commits.size());
  EXPECT_EQ(commit_merge->GetId(), unsynced_commits.back()->GetId());
  EXPECT_LT(commit_merge->GetTimestamp(), commit_c->GetTimestamp());
}

// Add a commit for which we don't have its parent. Verify that an error is
// returned, along with the id of the missing parent.
TEST_F(PageStorageTest, AddCommitsMissingParent) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit_parent = CommitImpl::FromContentAndParents(
      storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));
  parent.clear();
  parent.push_back(commit_parent->Clone());
  auto commit_child = CommitImpl::FromContentAndParents(
      storage_->GetCommitTracker(), environment_.clock(), root_identifier, std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit_child->GetId(), commit_child->GetStorageBytes().ToString());

  bool called;
  Status status;
  std::vector<CommitId> missing_ids;
  storage_->AddCommitsFromSync(
      std::move(commits_and_bytes), ChangeSource::P2P,
      callback::Capture(callback::SetWhenCalled(&called), &status, &missing_ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_NOT_FOUND, status);
  EXPECT_THAT(missing_ids, ElementsAre(commit_parent->GetId()));
}

TEST_F(PageStorageTest, GetMergeCommitIdsEmpty) {
  std::unique_ptr<const Commit> parent1 = TryCommitFromLocal(3);
  ASSERT_TRUE(parent1);

  std::unique_ptr<const Commit> parent2 = TryCommitFromLocal(3);
  ASSERT_TRUE(parent2);

  // Check that there is no merge of |parent1| and |parent2|.
  bool called;
  Status status;
  std::vector<CommitId> merges;
  storage_->GetMergeCommitIds(
      parent1->GetId(), parent2->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &merges));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_THAT(merges, IsEmpty());
}

TEST_F(PageStorageTest, GetMergeCommitIdsNonEmpty) {
  std::unique_ptr<const Commit> parent1 = TryCommitFromLocal(3);
  ASSERT_TRUE(parent1);

  std::unique_ptr<const Commit> parent2 = TryCommitFromLocal(3);
  ASSERT_TRUE(parent2);

  std::unique_ptr<Journal> journal = storage_->StartMergeCommit(parent1->Clone(), parent2->Clone());

  std::unique_ptr<const Commit> merge = TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(merge);

  // Check that |merge| is in the list of merges.
  bool called;
  Status status;
  std::vector<CommitId> merges;
  storage_->GetMergeCommitIds(
      parent1->GetId(), parent2->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &merges));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_THAT(merges, ElementsAre(merge->GetId()));

  storage_->GetMergeCommitIds(
      parent2->GetId(), parent1->GetId(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &merges));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_THAT(merges, ElementsAre(merge->GetId()));
}

TEST_F(PageStorageTest, AddLocalCommitsInterrupted) {
  // Destroy PageStorage while a local commit is in progress.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);

  // Destroy the PageStorageImpl object during the first async operation of
  // CommitJournal.
  async::PostTask(dispatcher(), [this]() { storage_.reset(); });
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  EXPECT_TRUE(RunLoopUntilIdle());
  // The callback is eaten by the destruction of |storage_|, so we are not
  // expecting to be called. However, we do not crash.
}

}  // namespace

}  // namespace storage
