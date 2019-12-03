// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/timekeeper/test_clock.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <queue>
#include <set>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/btree/iterator.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db.h"
#include "src/ledger/bin/storage/impl/page_db_empty_impl.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/split.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

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

  static void GetCommitRootIdentifier(const std::unique_ptr<PageStorageImpl>& storage,
                                      CommitIdView commit_id,
                                      fit::function<void(Status, ObjectIdentifier)> callback) {
    storage->GetCommitRootIdentifier(commit_id, std::move(callback));
  }

  static bool RootCommitIdentifierMapIsEmpty(const std::unique_ptr<PageStorageImpl>& storage) {
    return storage->roots_of_commits_being_added_.empty();
  }

  static bool RemoteCommitIdMapIsEmpty(const std::unique_ptr<PageStorageImpl>& storage) {
    return storage->remote_ids_of_commits_being_added_.empty();
  }

  static void ChooseDiffBases(const std::unique_ptr<PageStorageImpl>& storage,
                              CommitIdView target_id,
                              fit::callback<void(Status, std::vector<CommitId>)> callback) {
    return storage->ChooseDiffBases(std::move(target_id), std::move(callback));
  }

  // Asynchronous version of PageStorage::DeleteObject, running it inside a coroutine.
  static void DeleteObject(
      const std::unique_ptr<PageStorageImpl>& storage, ObjectDigest object_digest,
      fit::function<void(Status, ObjectReferencesAndPriority references)> callback) {
    storage->coroutine_manager_.StartCoroutine(
        [&storage, object_digest = std::move(object_digest),
         callback = std::move(callback)](coroutine::CoroutineHandler* handler) mutable {
          ObjectReferencesAndPriority references;
          Status status = storage->DeleteObject(handler, std::move(object_digest), &references);
          callback(status, references);
        });
  }

  static long CountLiveReferences(const std::unique_ptr<PageStorageImpl>& storage,
                                  const ObjectDigest& digest) {
    return storage->object_identifier_factory_.count(digest);
  }

  static callback::OperationSerializer& GetCommitSerializer(
      const std::unique_ptr<PageStorageImpl>& storage) {
    return storage->commit_serializer_;
  }
};

namespace {

using ::coroutine::CoroutineHandler;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyOfArray;
using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsSubsetOf;
using ::testing::IsSupersetOf;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;
using ::testing::VariantWith;

std::vector<PageStorage::CommitIdAndBytes> CommitAndBytesFromCommit(const Commit& commit) {
  std::vector<PageStorage::CommitIdAndBytes> result;
  result.emplace_back(commit.GetId(), commit.GetStorageBytes().ToString());
  return result;
}

testing::Matcher<const DeviceClock&> DeviceClockMatchesCommit(const Commit& commit) {
  return VariantWith<DeviceEntry>(
      Field("head", &DeviceEntry::head,
            AllOf(Field("commit_id", &ClockEntry::commit_id, commit.GetId()),
                  Field("generation", &ClockEntry::generation, commit.GetGeneration()))));
}

// Makes an object identifier untracked.
void UntrackIdentifier(ObjectIdentifier* identifier) {
  *identifier = ObjectIdentifier(identifier->key_index(), identifier->object_digest(), nullptr);
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
  FakeCommitWatcher() = default;

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

enum class HasP2P { YES, NO };

enum class HasCloud {
  // The cloud is present and supports |GetDiff|.
  YES_WITH_DIFFS,
  // The cloud is present but does not support |GetDiff|.
  YES_NO_DIFFS,
  // The cloud is not present.
  NO
};

// Combination of features of sync and storage we want to test for.
struct SyncFeatures {
  // Is P2P sync available?
  HasP2P has_p2p;
  // Is cloud sync available? Does the cloud support diffs?
  HasCloud has_cloud;
  // The diff compatibility policy used by Ledger.
  DiffCompatibilityPolicy diff_compatibility_policy;

  static const SyncFeatures kDefault;
  static const SyncFeatures kNoDiff;
};

const SyncFeatures SyncFeatures::kDefault = {HasP2P::YES, HasCloud::YES_WITH_DIFFS,
                                             DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES};
const SyncFeatures SyncFeatures::kNoDiff = {HasP2P::YES, HasCloud::YES_NO_DIFFS,
                                            DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES};

// Where an object is available from.
enum ObjectAvailability {
  // Object is only available from P2P.
  P2P,
  // Object is available from P2P and cloud.
  P2P_AND_CLOUD,
};

class DelayingFakeSyncDelegate : public PageSyncDelegate {
 public:
  explicit DelayingFakeSyncDelegate(fit::function<void(fit::closure)> on_get_object,
                                    fit::function<void(fit::closure)> on_get_diff = nullptr,
                                    SyncFeatures sync_features = SyncFeatures::kDefault)
      : features_(sync_features),
        on_get_object_(std::move(on_get_object)),
        on_get_diff_(std::move(on_get_diff)) {
    if (!on_get_diff_) {
      on_get_diff_ = [](fit::closure callback) { callback(); };
    }
  }

  // Adds the given object to sync. |object_source| indicates if it should be available from P2P
  // only, or from P2P and from the cloud.
  void AddObject(ObjectIdentifier object_identifier, const std::string& value,
                 ObjectAvailability object_source) {
    UntrackIdentifier(&object_identifier);
    auto [it, inserted] = digest_to_value_.emplace(std::move(object_identifier),
                                                   std::make_pair(object_source, value));
    if (!inserted && object_source == ObjectAvailability::P2P_AND_CLOUD) {
      // |P2P_AND_CLOUD| is more permissive than |P2P|.
      it->second.first = ObjectAvailability::P2P_AND_CLOUD;
    }
  }

  void GetObject(ObjectIdentifier object_identifier, RetrievedObjectType retrieved_object_type,
                 fit::function<void(Status, ChangeSource, IsObjectSynced,
                                    std::unique_ptr<DataSource::DataChunk>)>
                     callback) override {
    UntrackIdentifier(&object_identifier);
    object_requests.emplace(object_identifier, retrieved_object_type);
    auto value_found = digest_to_value_.find(object_identifier);
    if (value_found == digest_to_value_.end()) {
      callback(Status::INTERNAL_NOT_FOUND, ChangeSource::CLOUD, IsObjectSynced::NO, nullptr);
      return;
    }

    auto [object_source, value] = value_found->second;
    // Check we can return this object.
    if (features_.has_cloud != HasCloud::NO && retrieved_object_type == RetrievedObjectType::BLOB &&
        object_source == ObjectAvailability::P2P_AND_CLOUD) {
      on_get_object_([callback = std::move(callback), value = value] {
        callback(Status::OK, ChangeSource::CLOUD, IsObjectSynced::YES,
                 DataSource::DataChunk::Create(value));
      });
    } else if (features_.has_p2p == HasP2P::YES) {
      on_get_object_([callback = std::move(callback), value = value] {
        callback(Status::OK, ChangeSource::P2P, IsObjectSynced::NO,
                 DataSource::DataChunk::Create(value));
      });
    } else {
      callback(Status::INTERNAL_NOT_FOUND, ChangeSource::CLOUD, IsObjectSynced::NO, nullptr);
    }
  }

  void AddDiff(CommitId commit_id, CommitId base_id, std::vector<EntryChange> changes) {
    commit_to_diff_[commit_id] = std::make_pair(std::move(base_id), std::move(changes));
  }

  void GetDiff(CommitId commit_id, std::vector<CommitId> possible_bases,
               fit::function<void(Status status, CommitId base_commit,
                                  std::vector<EntryChange> diff_entries)>
                   callback) override {
    diff_requests.emplace(commit_id, std::move(possible_bases));
    switch (features_.has_cloud) {
      case HasCloud::NO:
        // We don't support diffs.
        callback(Status::INTERNAL_NOT_FOUND, {}, {});
        return;
      case HasCloud::YES_NO_DIFFS:
        // We only send diffs with base = target and empty changes.
        callback(Status::OK, commit_id, {});
        return;
      case HasCloud::YES_WITH_DIFFS:
        auto diff_found = commit_to_diff_.find(commit_id);
        if (diff_found == commit_to_diff_.end()) {
          callback(Status::INTERNAL_NOT_FOUND, {}, {});
          return;
        }
        callback(Status::OK, diff_found->second.first, diff_found->second.second);
    }
  }

  void UpdateClock(storage::Clock /*clock*/,
                   fit::function<void(ledger::Status)> callback) override {
    FXL_NOTIMPLEMENTED();
    callback(ledger::Status::NOT_IMPLEMENTED);
  }

  size_t GetNumberOfObjectsStored() { return digest_to_value_.size(); }

  std::set<std::pair<ObjectIdentifier, RetrievedObjectType>> object_requests;
  std::set<std::pair<CommitId, std::vector<CommitId>>> diff_requests;

  void set_on_get_object(fit::function<void(fit::closure)> callback) {
    on_get_object_ = std::move(callback);
  }

  void SetSyncFeatures(SyncFeatures features) { features_ = features; }

 private:
  SyncFeatures features_;
  fit::function<void(fit::closure)> on_get_object_;
  fit::function<void(fit::closure)> on_get_diff_;
  std::map<ObjectIdentifier, std::pair<ObjectAvailability, std::string>> digest_to_value_;
  std::map<CommitId, std::pair<CommitId, std::vector<EntryChange>>> commit_to_diff_;
};

class FakeSyncDelegate : public DelayingFakeSyncDelegate {
 public:
  FakeSyncDelegate(SyncFeatures sync_features = SyncFeatures::kDefault)
      : DelayingFakeSyncDelegate([](fit::closure callback) { callback(); },
                                 [](fit::closure callback) { callback(); }, sync_features) {}
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
      if (controller_->on_execute_) {
        controller_->on_execute_();
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

  Status HasPrefix(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView prefix) override {
    return leveldb_.HasPrefix(handler, prefix);
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

  // Sets a callback triggered before each |Batch::Execute|.
  void set_on_execute(fit::closure callback) { on_execute_ = std::move(callback); }

 private:
  // Number of calls to |Batch::Execute()| before they start failing. If
  // negative, |Batch::Execute()| calls will never fail.
  int fail_batch_execute_after_ = -1;
  LevelDb leveldb_;
  fit::closure on_execute_;
};

class PageStorageTest : public StorageTest {
 public:
  PageStorageTest() : PageStorageTest(ledger::kTestingGarbageCollectionPolicy) {}

  explicit PageStorageTest(GarbageCollectionPolicy gc_policy,
                           DiffCompatibilityPolicy diff_compatibility_policy =
                               DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES)
      : StorageTest(gc_policy, diff_compatibility_policy), encryption_service_(dispatcher()) {}

  PageStorageTest(const PageStorageTest&) = delete;
  PageStorageTest& operator=(const PageStorageTest&) = delete;
  ~PageStorageTest() override = default;

  // Test:
  void SetUp() override { ResetStorage(); }

  void ResetStorage(CommitPruningPolicy pruning_policy = CommitPruningPolicy::NEVER) {
    if (storage_) {
      storage_->SetSyncDelegate(nullptr);
      storage_.reset();
    }
    tmpfs_ = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
    PageId id = RandomString(environment_.random(), 10);
    auto db =
        std::make_unique<ControlledLevelDb>(dispatcher(), ledger::DetachedPath(tmpfs_->root_fd()));
    leveldb_ = db.get();
    ASSERT_EQ(db->Init(), Status::OK);
    storage_ = std::make_unique<PageStorageImpl>(&environment_, &encryption_service_, std::move(db),
                                                 id, pruning_policy);

    bool called;
    Status status;
    clocks::DeviceIdManagerEmptyImpl device_id_manager;
    storage_->Init(&device_id_manager,
                   callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(storage_->GetId(), id);
  }

  // After |UntrackIdentifier| or |ResetStorage|, |identifier| may point to an expired factory.
  // Reallocates a fresh identifier tracked by the current storage's factory if necessary.
  void RetrackIdentifier(ObjectIdentifier* identifier) {
    if (identifier->factory() != storage_->GetObjectIdentifierFactory()) {
      *identifier = storage_->GetObjectIdentifierFactory()->MakeObjectIdentifier(
          identifier->key_index(), identifier->object_digest());
    }
  }

 protected:
  PageStorage* GetStorage() override { return storage_.get(); }

  std::vector<std::unique_ptr<const Commit>> GetHeads() {
    std::vector<std::unique_ptr<const Commit>> heads;
    Status status = storage_->GetHeadCommits(&heads);
    EXPECT_EQ(status, Status::OK);
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
    EXPECT_EQ(status, Status::OK);
    return commit;
  }

  // Returns a random, tracked, non-inline object identifier.
  // Since random identifiers do not correspond to actual stored objects, we do not need to
  // untrack them to allow more garbage-collection opportunities (they wouldn't be collected
  // anyway). Keeping them tracked is necessary to satisfy validity checks within PageStorage
  // operations.
  ObjectIdentifier RandomObjectIdentifier() {
    return storage::RandomObjectIdentifier(environment_.random(),
                                           storage_->GetObjectIdentifierFactory());
  }

  // Returns a random, tracked, inline object identifier.
  ObjectIdentifier RandomInlineObjectIdentifier() {
    ObjectIdentifier identifier =
        MakeObject(RandomString(environment_.random(), 31), InlineBehavior::ALLOW)
            .object_identifier;
    EXPECT_TRUE(GetObjectDigestInfo(identifier.object_digest()).is_inlined());
    RetrackIdentifier(&identifier);
    return identifier;
  }

  // Returns an untracked ObjectData built with the provided |args|.
  template <typename... Args>
  ObjectData MakeObject(Args&&... args) {
    return ObjectData(&fake_factory_, std::forward<Args>(args)...);
  }

  std::unique_ptr<const Commit> TryCommitFromSync() {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
        environment_.clock(), environment_.random(), root_identifier, std::move(parent));

    bool called;
    Status status;
    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
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
    EXPECT_EQ(status, expected_status);
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
      auto key = absl::StrFormat("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      journal->Put(key, RandomObjectIdentifier(), KeyPriority::EAGER);
    }

    journal->Delete("key_does_not_exist");

    std::unique_ptr<const Commit> commit = TryCommitJournal(std::move(journal), Status::OK);
    if (!commit) {
      return commit;
    }

    // Check the contents.
    std::vector<Entry> entries = GetCommitContents(*commit);
    EXPECT_EQ(entries.size(), static_cast<size_t>(keys));
    for (int i = 0; i < keys; ++i) {
      auto key = absl::StrFormat("key%05d", i);
      if (key.size() < min_key_size) {
        key.resize(min_key_size);
      }
      EXPECT_EQ(entries[i].key, key);
    }

    return commit;
  }

  ObjectIdentifier TryAddFromLocal(std::string content,
                                   const ObjectIdentifier& expected_identifier) {
    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, DataSource::Create(std::move(content)), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, expected_identifier);
    return object_identifier;
  }

  std::unique_ptr<const Object> TryGetObject(ObjectIdentifier object_identifier,
                                             PageStorage::Location location,
                                             Status expected_status = Status::OK) {
    RetrackIdentifier(&object_identifier);
    bool called;
    Status status;
    std::unique_ptr<const Object> object;
    storage_->GetObject(object_identifier, location,
                        callback::Capture(callback::SetWhenCalled(&called), &status, &object));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, expected_status);
    return object;
  }

  fsl::SizedVmo TryGetObjectPart(ObjectIdentifier object_identifier, size_t offset, size_t max_size,
                                 PageStorage::Location location,
                                 Status expected_status = Status::OK) {
    RetrackIdentifier(&object_identifier);
    bool called;
    Status status;
    fsl::SizedVmo vmo;
    storage_->GetObjectPart(object_identifier, offset, max_size, location,
                            callback::Capture(callback::SetWhenCalled(&called), &status, &vmo));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, expected_status);
    return vmo;
  }

  std::unique_ptr<const Piece> TryGetPiece(ObjectIdentifier object_identifier,
                                           Status expected_status = Status::OK) {
    RetrackIdentifier(&object_identifier);
    bool called;
    Status status;
    std::unique_ptr<const Piece> piece;
    storage_->GetPiece(object_identifier,
                       callback::Capture(callback::SetWhenCalled(&called), &status, &piece));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, expected_status) << object_identifier;
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
    EXPECT_EQ(status, Status::OK);
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
    EXPECT_EQ(status, Status::OK);
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
    ASSERT_EQ(PageStorageImplAccessorForTest::GetDb(storage_).GetInboundObjectReferences(
                  handler, object_identifier, &stored_references),
              Status::OK);
    EXPECT_THAT(stored_references, UnorderedElementsAreArray(expected_references));
  }

  // Checks that |object_identifier| is referenced by |expected_references|.
  void CheckInboundCommitReferences(CoroutineHandler* handler, ObjectIdentifier object_identifier,
                                    const std::vector<CommitId>& expected_references) {
    ASSERT_FALSE(GetObjectDigestInfo(object_identifier.object_digest()).is_inlined())
        << "Broken test: CheckInboundCommitReferences must be called on "
           "non-inline pieces only.";
    std::vector<CommitId> stored_references;
    ASSERT_EQ(PageStorageImplAccessorForTest::GetDb(storage_).GetInboundCommitReferences(
                  handler, object_identifier, &stored_references),
              Status::OK);
    EXPECT_THAT(stored_references, UnorderedElementsAreArray(expected_references));
  }

  ::testing::AssertionResult ObjectIsUntracked(ObjectIdentifier object_identifier,
                                               bool expected_untracked) {
    RetrackIdentifier(&object_identifier);
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
    RetrackIdentifier(&object_identifier);
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

  ::testing::AssertionResult MarkCommitSynced(const CommitId& commit) {
    bool called;
    Status status;
    storage_->MarkCommitSynced(commit,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure() << "MarkCommitSynced did not return";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "MarkCommitSynced returned status " << status;
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult MarkPieceSynced(ObjectIdentifier object_identifier) {
    RetrackIdentifier(&object_identifier);
    bool called;
    Status status;
    storage_->MarkPieceSynced(object_identifier,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();

    if (!called) {
      return ::testing::AssertionFailure() << "MarkPieceSynced did not return";
    }
    if (status != Status::OK) {
      return ::testing::AssertionFailure() << "MarkPieceSynced returned status " << status;
    }
    return ::testing::AssertionSuccess();
  }

  ControlledLevelDb* leveldb_;
  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  std::unique_ptr<PageStorageImpl> storage_;
  // A fake factory to allocate test identifiers, ensuring they are not automatically tracked by
  // |storage_| (hence leaving more opportunities to find garbage-collection bugs).
  fake::FakeObjectIdentifierFactory fake_factory_;
};

// A PageStorage test with garbage-collection disabled.
class PageStorageTestNoGc : public PageStorageTest {
 public:
  PageStorageTestNoGc() : PageStorageTest(GarbageCollectionPolicy::NEVER) {}
};

// A PageStorage test with EAGER_ROOT_NODES garbage-collection policy.
class PageStorageTestEagerRootNodesGC : public PageStorageTest {
 public:
  PageStorageTestEagerRootNodesGC() : PageStorageTest(GarbageCollectionPolicy::EAGER_ROOT_NODES) {}
};

// A test fixture parametrized by what kind of queries will be answered by the sync delegate.
class PageStorageSyncTest : public PageStorageTest,
                            public ::testing::WithParamInterface<SyncFeatures> {
 public:
  PageStorageSyncTest() : PageStorageSyncTest(ledger::kTestingGarbageCollectionPolicy) {}
  explicit PageStorageSyncTest(GarbageCollectionPolicy gc_policy)
      : PageStorageTest(gc_policy, GetParam().diff_compatibility_policy) {}

  // Where are tree nodes expected to be available.
  ObjectAvailability TreeNodeObjectAvailability() {
    switch (GetParam().diff_compatibility_policy) {
      case DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES:
        return ObjectAvailability::P2P_AND_CLOUD;
      case DiffCompatibilityPolicy::USE_ONLY_DIFFS:
        return ObjectAvailability::P2P;
    }
  }
};

// Sync tests need one of P2P and cloud, and if they have only cloud, they need either
// |has_cloud| to be |YES_WITH_DIFFS| or |diff_compatibility_policy| to be
// |USE_DIFFS_AND_TREE_NODES|.
INSTANTIATE_TEST_SUITE_P(
    PageStorageSyncTest, PageStorageSyncTest,
    ::testing::Values(
        SyncFeatures{HasP2P::YES, HasCloud::YES_WITH_DIFFS,
                     DiffCompatibilityPolicy::USE_ONLY_DIFFS},
        SyncFeatures{HasP2P::YES, HasCloud::YES_WITH_DIFFS,
                     DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES},
        SyncFeatures{HasP2P::YES, HasCloud::YES_NO_DIFFS,
                     DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES},
        SyncFeatures{HasP2P::YES, HasCloud::YES_NO_DIFFS, DiffCompatibilityPolicy::USE_ONLY_DIFFS},
        SyncFeatures{HasP2P::YES, HasCloud::NO, DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES},
        SyncFeatures{HasP2P::YES, HasCloud::NO, DiffCompatibilityPolicy::USE_ONLY_DIFFS},
        SyncFeatures{HasP2P::NO, HasCloud::YES_NO_DIFFS,
                     DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES},
        SyncFeatures{HasP2P::NO, HasCloud::YES_WITH_DIFFS,
                     DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES},
        SyncFeatures{HasP2P::NO, HasCloud::YES_WITH_DIFFS,
                     DiffCompatibilityPolicy::USE_ONLY_DIFFS}));

TEST_F(PageStorageTest, AddGetLocalCommits) {
  // Search for a commit id that doesn't exist and see the error.
  bool called;
  Status status;
  std::unique_ptr<const Commit> lookup_commit;
  storage_->GetCommit(RandomCommitId(environment_.random()),
                      callback::Capture(callback::SetWhenCalled(&called), &status, &lookup_commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);
  EXPECT_FALSE(lookup_commit);

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  CommitId id = commit->GetId();
  std::string storage_bytes = commit->GetStorageBytes().ToString();

  // Search for a commit that exists and check the content.
  std::unique_ptr<const Commit> found = GetCommit(id);
  EXPECT_EQ(found->GetStorageBytes(), storage_bytes);
}

TEST_F(PageStorageTest, AddLocalCommitsReferences) {
  // Create two commits with the same root node. This requires creating an intermediate commit:
  // - insert entry A in the empty page, commit as commit 1
  // - insert entry B in commit 1, commit as commit 2
  // - remove entry B in commit 2, commit as commit 3
  // Then commits 1 and 3 have the same tree with the same entry ids, so will share a root node.
  // We then check that both commits 1 and 3 are stored as inbound references of their root node.

  std::unique_ptr<const Commit> base = GetFirstHead();
  const ObjectIdentifier object_id = RandomObjectIdentifier();

  std::unique_ptr<Journal> journal = storage_->StartCommit(base->Clone());
  journal->Put("key", object_id, KeyPriority::EAGER);
  bool called;
  Status status;
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(commit1->Clone());
  journal->Put("other", object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(commit2->Clone());
  journal->Delete("other");
  std::unique_ptr<const Commit> commit3;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit3));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  ObjectIdentifier root_node1 = commit1->GetRootIdentifier();
  ObjectIdentifier root_node3 = commit3->GetRootIdentifier();

  CommitId id1 = commit1->GetId();
  CommitId id3 = commit3->GetId();
  EXPECT_NE(id1, id3);
  EXPECT_EQ(root_node1, root_node3);

  RunInCoroutine([this, root_node1, id1, id3](CoroutineHandler* handler) {
    CheckInboundCommitReferences(handler, root_node1, {id1, id3});
  });
}

TEST_F(PageStorageTest, AddCommitFromLocalDoNotMarkUnsynedAlreadySyncedCommit) {
  bool called;
  Status status;

  // Create a conflict.
  std::unique_ptr<const Commit> base = GetFirstHead();

  std::unique_ptr<Journal> journal = storage_->StartCommit(base->Clone());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  CommitId id1 = commit1->GetId();
  storage_->MarkCommitSynced(id1, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(base->Clone());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  CommitId id2 = commit2->GetId();
  storage_->MarkCommitSynced(id2, callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // Make a merge commit. Merge commits only depend on their parents and
  // contents, so we can reproduce them.
  storage::ObjectIdentifier merged_object_id = RandomObjectIdentifier();
  journal = storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal->Put("key", merged_object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_merged1;
  storage_->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                &status, &commit_merged1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  CommitId merged_id1 = commit_merged1->GetId();

  auto commits = GetUnsyncedCommits();
  EXPECT_EQ(commits.size(), 1u);
  EXPECT_EQ(commits[0]->GetId(), merged_id1);

  storage_->MarkCommitSynced(merged_id1,
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // Add the commit again.
  journal = storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal->Put("key", merged_object_id, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_merged2;
  storage_->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                &status, &commit_merged2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  CommitId merged_id2 = commit_merged2->GetId();

  // Check that the commit is not marked unsynced.
  commits = GetUnsyncedCommits();
  EXPECT_EQ(commits.size(), 0u);
}

TEST_F(PageStorageTest, AddCommitBeforeParentsError) {
  // Try to add a commit before its parent and see the error.
  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(), &fake_factory_));
  ObjectIdentifier empty_object_id;
  GetEmptyNodeIdentifier(&empty_object_id);
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), empty_object_id, std::move(parent));

  bool called;
  Status status;
  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(std::move(commits_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&called), &status));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);
}

TEST_F(PageStorageTest, AddCommitsOutOfOrderError) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit1 = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_identifier, std::move(parent));
  parent.clear();
  parent.push_back(commit1->Clone());
  auto commit2 = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_identifier, std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit2->GetId(), commit2->GetStorageBytes().ToString());
  commits_and_bytes.emplace_back(commit1->GetId(), commit1->GetStorageBytes().ToString());

  bool called;
  Status status;
  storage_->AddCommitsFromSync(std::move(commits_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);
}

TEST_P(PageStorageSyncTest, AddGetSyncedCommits) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    FakeSyncDelegate sync(GetParam());
    storage_->SetSyncDelegate(&sync);

    // Create a node with 2 values.
    ObjectData lazy_value = MakeObject("Some data", InlineBehavior::PREVENT);
    ObjectData eager_value = MakeObject("More data", InlineBehavior::PREVENT);
    std::vector<Entry> entries = {
        Entry{"key0", lazy_value.object_identifier, KeyPriority::LAZY, EntryId("id_1")},
        Entry{"key1", eager_value.object_identifier, KeyPriority::EAGER, EntryId("id_2")},
    };
    std::unique_ptr<const btree::TreeNode> node;
    ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
    ObjectIdentifier root_identifier = node->GetIdentifier();

    // Add the three objects to FakeSyncDelegate.
    sync.AddObject(lazy_value.object_identifier, lazy_value.value,
                   ObjectAvailability::P2P_AND_CLOUD);
    sync.AddObject(eager_value.object_identifier, eager_value.value,
                   ObjectAvailability::P2P_AND_CLOUD);

    {
      // Ensure root_object is not kept, as the storage it depends on will be
      // deleted.
      std::unique_ptr<const Object> root_object =
          TryGetObject(root_identifier, PageStorage::Location::Local());

      fxl::StringView root_data;
      ASSERT_EQ(root_object->GetData(&root_data), Status::OK);
      sync.AddObject(root_identifier, root_data.ToString(), TreeNodeObjectAvailability());
    }

    // Reset and clear the storage.
    ResetStorage();
    storage_->SetSyncDelegate(&sync);
    RetrackIdentifier(&root_identifier);

    std::vector<std::unique_ptr<const Commit>> parent;
    parent.emplace_back(GetFirstHead());
    CommitId parent_id = parent[0]->GetId();
    std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
        environment_.clock(), environment_.random(), root_identifier, std::move(parent));
    CommitId id = commit->GetId();

    // Add the diff of the commit to FakeSyncDelegate.
    sync.AddDiff(id, parent_id, {{entries[0], false}, {entries[1], false}});

    // Adding the commit should only request the tree node and the eager value.
    // The diff should also be requested.
    sync.object_requests.clear();
    bool called;
    Status status;
    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(sync.diff_requests, ElementsAre(Pair(id, _)));

    // We only request the root object (at least as a tree node, maybe as a blob) and the eager
    // value (only as a BLOB).
    EXPECT_THAT(sync.object_requests,
                IsSupersetOf({Pair(root_identifier, RetrievedObjectType::TREE_NODE),
                              Pair(eager_value.object_identifier, RetrievedObjectType::BLOB)}));
    EXPECT_THAT(sync.object_requests,
                IsSubsetOf({Pair(root_identifier, RetrievedObjectType::TREE_NODE),
                            Pair(root_identifier, RetrievedObjectType::BLOB),
                            Pair(eager_value.object_identifier, RetrievedObjectType::BLOB)}));

    // Adding the same commit twice should not request any objects from sync.
    sync.object_requests.clear();
    sync.diff_requests.clear();
    storage_->AddCommitsFromSync(CommitAndBytesFromCommit(*commit), ChangeSource::CLOUD,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_TRUE(sync.object_requests.empty());
    EXPECT_TRUE(sync.diff_requests.empty());

    std::unique_ptr<const Commit> found = GetCommit(id);
    EXPECT_EQ(found->GetStorageBytes(), commit->GetStorageBytes());

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
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  CommitId id = commit->GetId();

  EXPECT_EQ(GetUnsyncedCommits().size(), 1u);

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
  storage_->AddCommitsFromSync(std::move(commits_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(GetUnsyncedCommits().size(), 0u);
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<const Commit>> commits = GetUnsyncedCommits();

  // Initially there should be no unsynced commits.
  EXPECT_TRUE(commits.empty());

  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  commits = GetUnsyncedCommits();
  EXPECT_EQ(commits.size(), 1u);
  EXPECT_EQ(commits[0]->GetStorageBytes(), commit->GetStorageBytes());

  // Mark it as synced.
  storage_->MarkCommitSynced(commit->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  commits = GetUnsyncedCommits();
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, HeadCommits) {
  // Every page should have one initial head commit.
  std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
  EXPECT_EQ(heads.size(), 1u);

  // Adding a new commit with the previous head as its parent should replace the
  // old head.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  heads = GetHeads();
  ASSERT_EQ(heads.size(), 1u);
  EXPECT_EQ(heads[0]->GetId(), commit->GetId());
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
    ObjectData value = MakeObject("value" + std::to_string(i), InlineBehavior::ALLOW);
    std::vector<Entry> entries = {Entry{"key" + std::to_string(i), value.object_identifier,
                                        KeyPriority::EAGER, EntryId("id" + std::to_string(i))}};
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
    std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
        &test_clock, environment_.random(), object_identifiers[i], std::move(parent));

    commits.emplace_back(commit->GetId(), commit->GetStorageBytes().ToString());
    sorted_commits.emplace_back(timestamps[i], commit->GetId());
  }

  auto rng = environment_.random()->NewBitGenerator<uint64_t>();
  std::shuffle(commits.begin(), commits.end(), rng);
  bool called;
  Status status;
  storage_->AddCommitsFromSync(std::move(commits), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // Check that GetHeadCommitIds returns sorted commits.
  std::vector<std::unique_ptr<const Commit>> heads;
  status = storage_->GetHeadCommits(&heads);
  EXPECT_EQ(status, Status::OK);
  std::sort(sorted_commits.begin(), sorted_commits.end());
  ASSERT_EQ(heads.size(), sorted_commits.size());
  for (size_t i = 0; i < sorted_commits.size(); ++i) {
    EXPECT_EQ(heads[i]->GetId(), sorted_commits[i].second);
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

  EXPECT_EQ(entries.size(), 500u);
  for (const auto& entry : entries) {
    EXPECT_EQ(entry.key.size(), 1024u);
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
      EXPECT_EQ(iteration_status, IterationStatus::DONE);
      for (const auto& identifier : sub_identifiers) {
        if (!GetObjectDigestInfo(identifier.object_digest()).is_inlined()) {
          EXPECT_EQ(unsynced_identifiers.count(identifier), 1u);
        }
      }
    }
  }
  EXPECT_TRUE(found_index);
}

TEST_F(PageStorageTest, DestroyUncommittedJournal) {
  // It is not an error if a journal is not committed or rolled back.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, data.object_identifier);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);
    EXPECT_EQ(piece->GetData(), data.value);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(object_identifier, false));
  });
}

// This test implements its own garbage-collection to discover bugs earlier and with better
// error messages.
TEST_F(PageStorageTestNoGc, AddHugeObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    // Create data large enough to be split into pieces (and trigger potential garbage-collection
    // bugs: the more pieces, the more likely we are to hit them).
    ObjectData data = MakeObject(RandomString(environment_.random(), 1 << 20));
    ASSERT_FALSE(GetObjectDigestInfo(data.object_identifier.object_digest()).is_inlined());
    ASSERT_FALSE(GetObjectDigestInfo(data.object_identifier.object_digest()).is_chunk());

    // Build a set of the pieces |data| is made of.
    std::set<ObjectDigest> digests;
    ForEachPiece(data.value, ObjectType::BLOB, &fake_factory_,
                 [&digests](std::unique_ptr<const Piece> piece) {
                   ObjectDigest digest = piece->GetIdentifier().object_digest();
                   if (GetObjectDigestInfo(digest).is_inlined()) {
                     return;
                   }
                   digests.insert(digest);
                 });

    // Trigger deletion of *all* pieces from storage immediately before *any* of them is written
    // to disk. This is an attempt at finding bugs in the code that wouldn't hold pieces alive
    // long enough before writing them to disk.
    leveldb_->set_on_execute([this, digests = std::move(digests)]() {
      for (const auto& digest : digests) {
        PageStorageImplAccessorForTest::DeleteObject(
            storage_, digest,
            [digest = digest](Status status, ObjectReferencesAndPriority references) {
              EXPECT_NE(status, Status::OK)
                  << "DeleteObject succeeded; missing a live reference for " << digest;
            });
      }
    });

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, data.object_identifier);

    std::unique_ptr<const Object> object =
        TryGetObject(object_identifier, PageStorage::Location::Local());
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->GetIdentifier(), data.object_identifier);
    fxl::StringView object_data;
    ASSERT_EQ(object->GetData(&object_data), Status::OK);
    EXPECT_EQ(convert::ToString(object_data), data.value);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(object_identifier, false));
  });
}

TEST_F(PageStorageTest, AddSmallObjectFromLocal) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data");

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, data.object_identifier);
    EXPECT_EQ(ExtractObjectDigestData(object_identifier.object_digest()), data.value);

    std::unique_ptr<const Piece> piece;
    EXPECT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
    // Inline objects do not need to ever be tracked.
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, false));
  });
}

TEST_F(PageStorageTest, InterruptAddObjectFromLocal) {
  ObjectData data = MakeObject("Some data");

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
  EXPECT_EQ(status, Status::IO_ERROR);
}

// This test deletes objects manually, do not use automatic garbage-collection to keep
// results predictable.
TEST_F(PageStorageTestNoGc, DeleteObject) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    auto data = std::make_unique<ObjectData>(storage_->GetObjectIdentifierFactory(), "Some data",
                                             InlineBehavior::PREVENT);
    ObjectIdentifier object_identifier = data->object_identifier;
    const ObjectDigest object_digest = object_identifier.object_digest();

    // Add a local piece |data|.
    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data->ToPiece(), ChangeSource::LOCAL, IsObjectSynced::NO, {},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Check that the piece can be read back.
    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);
    // The piece is a BLOB CHUNK, it should have no reference.
    ObjectReferencesAndPriority references;
    ASSERT_EQ(piece->AppendReferences(&references), Status::OK);
    EXPECT_THAT(references, IsEmpty());

    // Remove live references to the identifier.
    data.reset();
    piece.reset();
    UntrackIdentifier(&object_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, object_digest), 0);

    // Delete the piece.
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, object_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(references, IsEmpty());

    // Check that the object is gone.
    RetrackIdentifier(&object_identifier);
    EXPECT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
  });
}

// Converts ObjectIdentifier into ObjectDigest in |references| and returns the result.
ObjectReferencesAndPriority MakeObjectReferencesAndPriority(
    std::set<std::pair<ObjectIdentifier, KeyPriority>> references) {
  ObjectReferencesAndPriority result;
  std::transform(references.begin(), references.end(), std::inserter(result, result.begin()),
                 [](const std::pair<ObjectIdentifier, KeyPriority>& reference)
                     -> std::pair<ObjectDigest, KeyPriority> {
                   return {reference.first.object_digest(), reference.second};
                 });
  return result;
}

// Tests that DeleteObject deletes both piece references and tree references.
// This test deletes objects manually, do not use automatic garbage-collection to keep
// results predictable.
TEST_F(PageStorageTestNoGc, DeleteObjectWithReferences) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    // Create a valid random tree node object, with tree references and large enough to be split
    // into several pieces.
    std::vector<Entry> entries;
    std::map<size_t, ObjectIdentifier> children;
    std::set<std::pair<ObjectIdentifier, KeyPriority>> references;
    for (size_t i = 0; i < 100; ++i) {
      // Add a random entry.
      entries.push_back(Entry{RandomString(environment_.random(), 500), RandomObjectIdentifier(),
                              i % 2 ? KeyPriority::EAGER : KeyPriority::LAZY,
                              EntryId(RandomString(environment_.random(), 32))});
      references.insert({entries.back().object_identifier, entries.back().priority});
      // Add a random child.
      children.emplace(i, RandomObjectIdentifier());
      references.insert({children[i], KeyPriority::EAGER});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& e1, const Entry& e2) { return e1.key < e2.key; });
    std::string data_str = btree::EncodeNode(0, entries, children);
    ASSERT_TRUE(btree::CheckValidTreeNodeSerialization(data_str));

    // Add the tree node to local storage.
    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::TREE_NODE,
        MakeObject(std::move(data_str), ObjectType::TREE_NODE, InlineBehavior::PREVENT)
            .ToDataSource(),
        MakeObjectReferencesAndPriority(references),
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Check that we got an index piece, hence some piece references.
    ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
    std::unique_ptr<const Piece> piece = TryGetPiece(object_identifier);
    ASSERT_NE(piece, nullptr);

    // Add piece references to |references| to check both tree and piece references from now on.
    ASSERT_EQ(ForEachIndexChild(
                  piece->GetData(), &fake_factory_,
                  [object_identifier, &references](ObjectIdentifier piece_identifier) {
                    if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
                      // References to inline pieces are not stored on disk.
                      return Status::OK;
                    }
                    references.insert({piece_identifier, KeyPriority::EAGER});
                    return Status::OK;
                  }),
              Status::OK);

    // Check piece and tree have been written to local storage.
    for (const auto& [identifier, priority] : references) {
      CheckInboundObjectReferences(handler, identifier,
                                   {{object_identifier.object_digest(), priority}});
    }

    // Remove live references to the identifier.
    const ObjectDigest object_digest = object_identifier.object_digest();
    piece.reset();
    UntrackIdentifier(&object_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, object_digest), 0);

    // Delete the piece.
    ObjectReferencesAndPriority delete_references;
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, object_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &delete_references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(delete_references, ContainerEq(MakeObjectReferencesAndPriority(references)));

    // Check that the object is gone.
    RetrackIdentifier(&object_identifier);
    EXPECT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);

    // Check that references are gone.
    for (const auto& [identifier, priority] : references) {
      CheckInboundObjectReferences(handler, identifier, {});
    }
  });
}

// This test creates two commits, commit1 which associates "key" to some "Some data", and commit2
// which associates "key" to a random piece.
// It first attempts to delete the root piece of commit1 and the piece containing "Some data",
// which is impossible because those pieces are referenced by commit1 and its root piece
// respectively. It then marks the root piece of commit1 as synchronized. This makes another
// attempt at the same deletions succeed: the root piece is now synchronized and referenced by a
// non-head commit, and the piece containing "Some data" is not referenced by anything (after the
// former deletion succeeds), making both of them garbage-collectable. This test deletes objects
// manually, do not use automatic garbage-collection to keep results predictable.
TEST_F(PageStorageTestNoGc, DeleteObjectAbortsWhenOnDiskReference) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    auto data = std::make_unique<ObjectData>(&fake_factory_, "Some data", InlineBehavior::PREVENT);
    ObjectIdentifier object_identifier = data->object_identifier;
    const ObjectDigest object_digest = object_identifier.object_digest();
    RetrackIdentifier(&object_identifier);

    // Add a local piece |data|.
    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data->ToPiece(), ChangeSource::LOCAL, IsObjectSynced::NO, {},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Add an object-object on-disk reference, as part of a commit.
    std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
    journal->Put("key", object_identifier, KeyPriority::EAGER);
    std::unique_ptr<const Commit> commit1;
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Mark the commit as synced so that we do not need to keep its root commit alive to compute a
    // cloud diff.
    storage_->MarkCommitSynced(commit1->GetId(),
                               callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Record the root piece of |commit1| to attempt deletion later.
    ObjectIdentifier root_piece_identifier = commit1->GetRootIdentifier();
    const ObjectDigest root_piece_digest = root_piece_identifier.object_digest();

    // Remove live references to the identifiers.
    data.reset();
    commit1.reset();
    UntrackIdentifier(&object_identifier);
    UntrackIdentifier(&root_piece_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, object_digest), 0);

    // Add another commit so that the previous commit is not live anymore (otherwise it keeps a
    // live reference to its root piece |root_piece_digest|).
    EXPECT_NE(PageStorageImplAccessorForTest::CountLiveReferences(storage_, root_piece_digest), 0);
    journal = storage_->StartCommit(GetFirstHead());
    journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
    std::unique_ptr<const Commit> commit2;
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Mark the commit as synced so that we do not need to keep its parent alive to compute a
    // cloud diff (otherwise it will also keep a live reference to |root_piece_digest|).
    storage_->MarkCommitSynced(commit2->GetId(),
                               callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, root_piece_digest), 0);

    // Attempt to delete the |data| piece.
    ObjectReferencesAndPriority references;
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, object_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::CANCELED);

    // Attempt to delete the root piece of |commit1|.
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, root_piece_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::CANCELED);

    // Check that the pieces are is still there.
    RetrackIdentifier(&object_identifier);
    RetrackIdentifier(&root_piece_identifier);
    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);
    ASSERT_EQ(ReadObject(handler, root_piece_identifier, &piece), Status::OK);
    piece.reset();

    // Mark the root piece of |commit1| as synced, to make it garbage-collectable (commit-object
    // references are ignored for synchronized pieces).
    RunInCoroutine([this, root_piece_identifier](CoroutineHandler* handler) {
      EXPECT_EQ(PageStorageImplAccessorForTest::GetDb(storage_).SetObjectStatus(
                    handler, root_piece_identifier, PageDbObjectStatus::SYNCED),
                Status::OK);
    });

    // Delete the root piece of |commit1|.
    UntrackIdentifier(&root_piece_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, root_piece_digest), 0);
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, root_piece_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    RetrackIdentifier(&root_piece_identifier);
    ASSERT_EQ(ReadObject(handler, root_piece_identifier, &piece), Status::INTERNAL_NOT_FOUND);

    // Since tree references are associated with the root piece, it is now possible to delete the
    // |data| piece, which was only referenced at |commit1|.
    UntrackIdentifier(&object_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, object_digest), 0);
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, object_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    RetrackIdentifier(&object_identifier);
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
  });
}

// This test deletes objects manually, do not use automatic garbage-collection to keep
// results predictable.
TEST_F(PageStorageTestNoGc, DeleteObjectAbortsWhenLiveReference) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    auto data = std::make_unique<ObjectData>(&fake_factory_, "Some data", InlineBehavior::PREVENT);
    ObjectIdentifier object_identifier = data->object_identifier;
    const ObjectDigest object_digest = object_identifier.object_digest();

    // Add a local piece |data|.
    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data->ToPiece(), ChangeSource::LOCAL, IsObjectSynced::NO, {},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Remove live references to the identifier.
    data.reset();
    UntrackIdentifier(&object_identifier);
    EXPECT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(storage_, object_digest), 0);

    // Start deletion of the piece.
    ObjectReferencesAndPriority references;
    PageStorageImplAccessorForTest::DeleteObject(
        storage_, object_digest,
        callback::Capture(callback::SetWhenCalled(&called), &status, &references));
    ASSERT_FALSE(called);

    // Make the identifier live again before deletion has gone through.
    RetrackIdentifier(&object_identifier);

    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::CANCELED);

    // Check that the object is still there.
    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);
  });
}

TEST_F(PageStorageTest, AddLocalPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);
    const ObjectIdentifier reference = RandomObjectIdentifier();

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::LOCAL, IsObjectSynced::NO,
        {{reference.object_digest(), KeyPriority::LAZY}},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, data.object_identifier, &piece), Status::OK);
    EXPECT_EQ(piece->GetData(), data.value);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));

    CheckInboundObjectReferences(handler, reference,
                                 {{data.object_identifier.object_digest(), KeyPriority::LAZY}});
  });
}

TEST_F(PageStorageTest, AddSyncPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);
    const ObjectIdentifier reference = RandomObjectIdentifier();

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::CLOUD, IsObjectSynced::YES,
        {{reference.object_digest(), KeyPriority::EAGER}},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, data.object_identifier, &piece), Status::OK);
    EXPECT_EQ(piece->GetData(), data.value);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, true));

    CheckInboundObjectReferences(handler, reference,
                                 {{data.object_identifier.object_digest(), KeyPriority::EAGER}});
  });
}

TEST_F(PageStorageTest, AddP2PPiece) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    PageStorageImplAccessorForTest::AddPiece(
        storage_, data.ToPiece(), ChangeSource::P2P, IsObjectSynced::NO, {},
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, data.object_identifier, &piece), Status::OK);
    EXPECT_EQ(piece->GetData(), data.value);
    EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));
    EXPECT_TRUE(IsPieceSynced(data.object_identifier, false));
  });
}

TEST_F(PageStorageTest, GetObject) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);
    ASSERT_EQ(WriteObject(handler, &data), Status::OK);

    std::unique_ptr<const Object> object =
        TryGetObject(data.object_identifier, PageStorage::Location::Local());
    EXPECT_EQ(object->GetIdentifier(), data.object_identifier);
    fxl::StringView object_data;
    ASSERT_EQ(object->GetData(&object_data), Status::OK);
    EXPECT_EQ(convert::ToString(object_data), data.value);
  });
}

TEST_F(PageStorageTest, GetObjectPart) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("_Some data_", InlineBehavior::PREVENT);
    ASSERT_EQ(WriteObject(handler, &data), Status::OK);

    fsl::SizedVmo object_part =
        TryGetObjectPart(data.object_identifier, 1, data.size - 2, PageStorage::Location::Local());
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(convert::ToString(object_part_data), data.value.substr(1, data.size - 2));
  });
}

TEST_F(PageStorageTest, GetObjectPartLargeOffset) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("_Some data_", InlineBehavior::PREVENT);
    ASSERT_EQ(WriteObject(handler, &data), Status::OK);

    fsl::SizedVmo object_part = TryGetObjectPart(data.object_identifier, data.size * 2, data.size,
                                                 PageStorage::Location::Local());
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(convert::ToString(object_part_data), "");
  });
}

TEST_F(PageStorageTest, GetObjectPartLargeMaxSize) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("_Some data_", InlineBehavior::PREVENT);
    ASSERT_EQ(WriteObject(handler, &data), Status::OK);

    fsl::SizedVmo object_part =
        TryGetObjectPart(data.object_identifier, 0, data.size * 2, PageStorage::Location::Local());
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(convert::ToString(object_part_data), data.value);
  });
}

TEST_F(PageStorageTest, GetObjectPartNegativeArgs) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("_Some data_", InlineBehavior::PREVENT);
    ASSERT_EQ(WriteObject(handler, &data), Status::OK);

    fsl::SizedVmo object_part = TryGetObjectPart(data.object_identifier, -data.size + 1, -1,
                                                 PageStorage::Location::Local());
    std::string object_part_data;
    ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
    EXPECT_EQ(convert::ToString(object_part_data), data.value.substr(1, data.size - 1));
  });
}

TEST_F(PageStorageTest, GetLargeObjectPart) {
  std::string data_str = RandomString(environment_.random(), 65536);
  size_t offset = 6144;
  size_t size = 49152;

  ObjectData data = MakeObject(std::move(data_str), InlineBehavior::PREVENT);

  ASSERT_EQ(GetObjectDigestInfo(data.object_identifier.object_digest()).piece_type,
            PieceType::INDEX);

  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      ObjectType::BLOB, data.ToDataSource(), /*tree_references=*/{},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(object_identifier, data.object_identifier);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::Local());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  std::string result_str = convert::ToString(object_part_data);
  EXPECT_EQ(result_str.size(), size);
  EXPECT_EQ(result_str, data.value.substr(offset, size));
}

TEST_F(PageStorageTest, GetObjectPartFromSync) {
  ObjectData data = MakeObject("_Some data_", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data.value, ObjectAvailability::P2P_AND_CLOUD);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part = TryGetObjectPart(data.object_identifier, 1, data.size - 2,
                                               PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data.value.substr(1, data.size - 2));

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data = MakeObject("_Some other data_", InlineBehavior::PREVENT);
  TryGetObjectPart(other_data.object_identifier, 1, other_data.size - 2,
                   PageStorage::Location::Local(), Status::INTERNAL_NOT_FOUND);
  TryGetObjectPart(other_data.object_identifier, 1, other_data.size - 2,
                   PageStorage::Location::ValueFromNetwork(), Status::NETWORK_ERROR);
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
      data_str, ObjectType::BLOB, &fake_factory_,
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
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  // Read 128 bytes off the end of the first chunk.
  uint64_t size = 128;
  ASSERT_LT(size, chunk_lengths[0]);
  uint64_t offset = chunk_lengths[0] - size;

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str.substr(offset, size));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(Pair(object_identifier, RetrievedObjectType::BLOB)));
  EXPECT_THAT(sync.object_requests,
              Contains(Pair(chunk_identifiers[0], RetrievedObjectType::BLOB)));
  EXPECT_THAT(sync.object_requests, Not(Contains(Pair(chunk_identifiers[1], _))));
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
      data_str, ObjectType::BLOB, &fake_factory_,
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
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  // Read 128 bytes off the start of the second chunk.
  uint64_t size = 128;
  ASSERT_LT(size, chunk_lengths[1]);
  uint64_t offset = chunk_lengths[0];

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str.substr(offset, size));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(Pair(object_identifier, RetrievedObjectType::BLOB)));
  EXPECT_THAT(sync.object_requests, Not(Contains(Pair(chunk_identifiers[0], _))));
  EXPECT_THAT(sync.object_requests,
              Contains(Pair(chunk_identifiers[1], RetrievedObjectType::BLOB)));
}

TEST_F(PageStorageTest, GetObjectPartFromSyncZeroBytes) {
  // Generates a read that falls inside a chunk but reads zero bytes.
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);

  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::BLOB, &fake_factory_, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        ObjectDigestInfo digest_info = GetObjectDigestInfo(object_identifier.object_digest());
        if (digest_info.is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  // Read zero bytes inside a chunk. This succeeds and only reads the root
  // piece.
  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, 12, 0, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), "");
  EXPECT_THAT(sync.object_requests,
              ElementsAre(Pair(object_identifier, RetrievedObjectType::BLOB)));
}

TEST_F(PageStorageTest, GetObjectPartFromSyncZeroBytesNotFound) {
  FakeSyncDelegate sync;
  storage_->SetSyncDelegate(&sync);

  // Reading zero bytes from non-existing objects returns an error.
  ObjectData other_data = MakeObject("_Some other data_", InlineBehavior::PREVENT);
  TryGetObjectPart(other_data.object_identifier, 1, 0, PageStorage::Location::ValueFromNetwork(),
                   Status::INTERNAL_NOT_FOUND);
}

// This test implements its own garbage-collection to discover bugs earlier and with better
// error messages.
TEST_F(PageStorageTestNoGc, GetHugeObjectPartFromSync) {
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);
  int64_t offset = 28672;
  int64_t size = 128;

  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB, &fake_factory_,
                   [&sync, &digest_to_identifier](std::unique_ptr<const Piece> piece) {
                     ObjectIdentifier object_identifier = piece->GetIdentifier();
                     if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
                       return;
                     }
                     digest_to_identifier[object_identifier.object_digest()] = object_identifier;
                     sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                                    ObjectAvailability::P2P_AND_CLOUD);
                   });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  // Trigger deletion of *all* pieces from storage immediately after *any* of them is retrieved
  // from cloud. This is an attempt at finding bugs in the code that wouldn't hold pieces alive
  // long enough before writing them to disk.
  sync.set_on_get_object([this, &digest_to_identifier](fit::closure callback) {
    callback();
    for (const auto& [digest, identifier] : digest_to_identifier) {
      PageStorageImplAccessorForTest::DeleteObject(
          storage_, digest,
          [digest = digest](Status status, ObjectReferencesAndPriority references) {
            EXPECT_NE(status, Status::OK)
                << "DeleteObject succeeded; missing a live reference for " << digest;
          });
    }
  });
  storage_->SetSyncDelegate(&sync);

  // Add a commit lazily referencing the object to keep it alive once downloaded.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  RetrackIdentifier(&object_identifier);
  journal->Put("key", object_identifier, KeyPriority::LAZY);
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  UntrackIdentifier(&object_identifier);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str.substr(offset, size));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(Pair(object_identifier, RetrievedObjectType::BLOB)));
  // Check that the requested pieces have been added to storage, and collect
  // their outbound references into an inbound-references map. Note that we need
  // to collect references only from piece actually added to storage, rather
  // than all pieces from |ForEachPiece|, since pieces not present in storage do
  // not contribute to reference counting.
  std::map<ObjectIdentifier, ObjectReferencesAndPriority> inbound_references;
  for (const auto& [piece_identifier, object_type] : sync.object_requests) {
    EXPECT_EQ(object_type, RetrievedObjectType::BLOB);

    auto piece = TryGetPiece(piece_identifier);
    ASSERT_NE(piece, nullptr);
    ObjectReferencesAndPriority outbound_references;
    ASSERT_EQ(Status::OK, piece->AppendReferences(&outbound_references));
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
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::BLOB, &fake_factory_, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str.substr(data_str.size() + offset, size));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  // Check that at least the root piece has been added to storage.
  TryGetPiece(object_identifier);
}

TEST_F(PageStorageTest, GetHugeObjectFromSyncMaxConcurrentDownloads) {
  // In practice, a string that long yields between 30 and 60 pieces.
  std::string data_str = RandomString(environment_.random(), 2 << 18);

  // Create a fake sync delegate that will accumulate pending calls in |sync_delegate_calls|.
  std::vector<fit::closure> sync_delegate_calls;
  DelayingFakeSyncDelegate sync([&sync_delegate_calls](fit::closure callback) {
    sync_delegate_calls.push_back(std::move(callback));
  });
  storage_->SetSyncDelegate(&sync);

  // Initialize the sync delegate with the pieces of |data|.
  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::BLOB, &fake_factory_,
                   [&sync, &digest_to_identifier](std::unique_ptr<const Piece> piece) {
                     ObjectIdentifier object_identifier = piece->GetIdentifier();
                     if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
                       return;
                     }
                     digest_to_identifier[object_identifier.object_digest()] = object_identifier;
                     sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                                    ObjectAvailability::P2P_AND_CLOUD);
                   });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  // Check that we created a part big enough to require at least two batches of pending calls.
  ASSERT_GT(sync.GetNumberOfObjectsStored(), 2 * kMaxConcurrentDownloads);
  RetrackIdentifier(&object_identifier);

  // Fetch the whole object from the delegate. This should block repeateadly whenever we have
  // accumulated the maximum number of concurrent connections, ie. pending calls.
  bool called;
  Status status;
  fsl::SizedVmo object_part;
  storage_->GetObjectPart(
      object_identifier, /*offset=*/0, /*max_size=*/-1, PageStorage::Location::ValueFromNetwork(),
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_part));
  RunLoopUntilIdle();

  // Unblock the pending calls until GetObjectPart returns.
  do {
    EXPECT_LE(sync_delegate_calls.size(), kMaxConcurrentDownloads);
    for (auto& sync_delegate_call : sync_delegate_calls) {
      async::PostTask(dispatcher(), [sync_delegate_call = std::move(sync_delegate_call)]() {
        sync_delegate_call();
      });
    }
    sync_delegate_calls.clear();
    RunLoopUntilIdle();
  } while (!called);

  EXPECT_EQ(status, Status::OK);
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str);
  EXPECT_EQ(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  EXPECT_THAT(sync.object_requests, Contains(Pair(object_identifier, RetrievedObjectType::BLOB)));
}

TEST_F(PageStorageTest, GetObjectFromSync) {
  ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data.value, ObjectAvailability::P2P_AND_CLOUD);
  storage_->SetSyncDelegate(&sync);

  std::unique_ptr<const Object> object =
      TryGetObject(data.object_identifier, PageStorage::Location::ValueFromNetwork());
  EXPECT_EQ(object->GetIdentifier(), data.object_identifier);
  fxl::StringView object_data;
  ASSERT_EQ(object->GetData(&object_data), Status::OK);
  EXPECT_EQ(convert::ToString(object_data), data.value);
  // Check that the piece has been added to storage (it is small enough that
  // there is only one piece).
  TryGetPiece(data.object_identifier);

  storage_->SetSyncDelegate(nullptr);
  ObjectData other_data = MakeObject("Some other data", InlineBehavior::PREVENT);
  TryGetObject(other_data.object_identifier, PageStorage::Location::Local(),
               Status::INTERNAL_NOT_FOUND);
  TryGetObject(other_data.object_identifier, PageStorage::Location::ValueFromNetwork(),
               Status::NETWORK_ERROR);
}

TEST_F(PageStorageTest, FullDownloadAfterPartial) {
  std::string data_str = RandomString(environment_.random(), 2 * 65536 + 1);
  int64_t offset = 0;
  int64_t size = 128;

  FakeSyncDelegate sync;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::BLOB, &fake_factory_, [&sync](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier object_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
          return;
        }
        sync.AddObject(std::move(object_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  // Add a commit lazily referencing the object to keep it alive once downloaded.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  RetrackIdentifier(&object_identifier);
  journal->Put("key", object_identifier, KeyPriority::LAZY);
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  UntrackIdentifier(&object_identifier);

  fsl::SizedVmo object_part =
      TryGetObjectPart(object_identifier, offset, size, PageStorage::Location::ValueFromNetwork());
  std::string object_part_data;
  ASSERT_TRUE(fsl::StringFromVmo(object_part, &object_part_data));
  EXPECT_EQ(convert::ToString(object_part_data), data_str.substr(offset, size));
  EXPECT_LT(sync.object_requests.size(), sync.GetNumberOfObjectsStored());
  TryGetObject(object_identifier, PageStorage::Location::Local(), Status::INTERNAL_NOT_FOUND);
  // Check that all requested pieces have been stored locally.
  for (const auto& [piece_identifier, object_type] : sync.object_requests) {
    ASSERT_EQ(object_type, RetrievedObjectType::BLOB);
    TryGetPiece(piece_identifier);
  }

  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::ValueFromNetwork());
  fxl::StringView object_data;
  ASSERT_EQ(object->GetData(&object_data), Status::OK);
  EXPECT_EQ(convert::ToString(object_data), data_str);
  EXPECT_EQ(sync.GetNumberOfObjectsStored(), sync.object_requests.size());
  TryGetObject(object_identifier, PageStorage::Location::Local(), Status::OK);
  // Check that all pieces have been stored locally.
  for (const auto& [piece_identifier, object_type] : sync.object_requests) {
    ASSERT_EQ(object_type, RetrievedObjectType::BLOB);
    TryGetPiece(piece_identifier);
  }
}

TEST_F(PageStorageTest, GetObjectFromSyncWrongId) {
  ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);
  ObjectData data2 = MakeObject("Some data2", InlineBehavior::PREVENT);
  FakeSyncDelegate sync;
  sync.AddObject(data.object_identifier, data2.value, ObjectAvailability::P2P_AND_CLOUD);
  storage_->SetSyncDelegate(&sync);

  TryGetObject(data.object_identifier, PageStorage::Location::ValueFromNetwork(),
               Status::DATA_INTEGRITY_ERROR);
}

TEST_F(PageStorageTest, AddAndGetHugeTreenodeFromLocal) {
  std::string data_str = RandomString(environment_.random(), 65536);

  ObjectData data = MakeObject(std::move(data_str), ObjectType::TREE_NODE, InlineBehavior::PREVENT);
  // An identifier to another tree node pointed at by the current one.
  const ObjectIdentifier tree_reference = RandomObjectIdentifier();
  ASSERT_EQ(GetObjectDigestInfo(data.object_identifier.object_digest()).object_type,
            ObjectType::TREE_NODE);
  ASSERT_EQ(GetObjectDigestInfo(data.object_identifier.object_digest()).piece_type,
            PieceType::INDEX);
  ASSERT_EQ(GetObjectDigestInfo(data.object_identifier.object_digest()).inlined, InlinedPiece::NO);

  bool called;
  Status status;
  ObjectIdentifier object_identifier;
  storage_->AddObjectFromLocal(
      ObjectType::TREE_NODE, data.ToDataSource(),
      {{tree_reference.object_digest(), KeyPriority::LAZY}},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_EQ(status, Status::OK);
  // This ensures that the object is encoded with an index, as we checked the
  // piece type of |data.object_identifier| above.
  EXPECT_EQ(object_identifier, data.object_identifier);

  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::Local());
  fxl::StringView content;
  ASSERT_EQ(object->GetData(&content), Status::OK);
  EXPECT_EQ(content, data.value);
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
    ASSERT_EQ(ForEachIndexChild(
                  piece->GetData(), &fake_factory_,
                  [this, handler, object_identifier](ObjectIdentifier piece_identifier) {
                    if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
                      // References to inline pieces are not stored on disk.
                      return Status::OK;
                    }
                    CheckInboundObjectReferences(
                        handler, piece_identifier,
                        {{object_identifier.object_digest(), KeyPriority::EAGER}});
                    return Status::OK;
                  }),
              Status::OK);
  });
}

// This test implements its own garbage-collection to discover bugs earlier and with better
// error messages.
TEST_F(PageStorageTestNoGc, AddAndGetHugeTreenodeFromSync) {
  // Build a random, valid tree node.
  std::vector<Entry> entries;
  std::map<size_t, ObjectIdentifier> children;
  for (size_t i = 0; i < 1000; ++i) {
    entries.push_back(Entry{RandomString(environment_.random(), 50), RandomObjectIdentifier(),
                            i % 2 ? KeyPriority::EAGER : KeyPriority::LAZY,
                            EntryId(RandomString(environment_.random(), 32))});
    children.emplace(i, RandomObjectIdentifier());
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry& e1, const Entry& e2) { return e1.key < e2.key; });
  std::string data_str = btree::EncodeNode(0, entries, children);
  ASSERT_TRUE(btree::CheckValidTreeNodeSerialization(data_str));

  // Split the tree node content into pieces, add them to a SyncDelegate to be
  // retrieved by GetObject, and store inbound piece references into a map to
  // check them later.
  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  FakeSyncDelegate sync(SyncFeatures::kNoDiff);
  std::map<ObjectIdentifier, ObjectReferencesAndPriority> inbound_references;
  ObjectIdentifier object_identifier = ForEachPiece(
      data_str, ObjectType::TREE_NODE, &fake_factory_,
      [&sync, &digest_to_identifier, &inbound_references](std::unique_ptr<const Piece> piece) {
        ObjectIdentifier piece_identifier = piece->GetIdentifier();
        if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
          return;
        }
        digest_to_identifier[piece_identifier.object_digest()] = piece_identifier;
        ObjectReferencesAndPriority outbound_references;
        ASSERT_EQ(Status::OK, piece->AppendReferences(&outbound_references));
        for (const auto& [reference, priority] : outbound_references) {
          auto reference_identifier = digest_to_identifier.find(reference);
          // ForEachPiece returns pieces in order, so we must have already seen
          // pieces referenced by the current one.
          ASSERT_NE(reference_identifier, digest_to_identifier.end());
          inbound_references[reference_identifier->second].emplace(piece_identifier.object_digest(),
                                                                   priority);
        }
        sync.AddObject(std::move(piece_identifier), piece->GetData().ToString(),
                       ObjectAvailability::P2P_AND_CLOUD);
      });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  // Trigger deletion of *all* pieces from storage immediately after *any* of them is retrieved
  // from cloud. This is an attempt at finding bugs in the code that wouldn't hold pieces alive
  // long enough before writing them to disk.
  sync.set_on_get_object([this, &digest_to_identifier](fit::closure callback) {
    callback();
    for (const auto& [digest, identifier] : digest_to_identifier) {
      PageStorageImplAccessorForTest::DeleteObject(
          storage_, digest,
          [digest = digest](Status status, ObjectReferencesAndPriority references) {
            EXPECT_NE(status, Status::OK)
                << "DeleteObject succeeded; missing a live reference for " << digest;
          });
    }
  });
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

  // The tree node is not in a commit, but we still need to put the id of a commit we know in the
  // location. Since diffs are disabled, this can be any commit.
  RetrackIdentifier(&object_identifier);
  CommitId commit_id = GetFirstHead()->GetId();
  std::unique_ptr<const Object> object =
      TryGetObject(object_identifier, PageStorage::Location::TreeNodeFromNetwork(commit_id));
  fxl::StringView content;
  ASSERT_EQ(object->GetData(&content), Status::OK);
  EXPECT_EQ(content, data_str);

  // Check that all pieces have been stored locally.
  EXPECT_EQ(sync.GetNumberOfObjectsStored(), sync.object_requests.size());
  for (auto [piece_identifier, object_type] : sync.object_requests) {
    EXPECT_EQ(object_type, RetrievedObjectType::TREE_NODE);
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
  auto local_object = TryGetObject(object_identifier, PageStorage::Location::Local(), Status::OK);
  ASSERT_EQ(object->GetData(&content), Status::OK);
  EXPECT_EQ(content, data_str);
}

TEST_F(PageStorageTest, UnsyncedPieces) {
  ObjectData data_array[] = {
      MakeObject("Some data", InlineBehavior::PREVENT),
      MakeObject("Some more data", InlineBehavior::PREVENT),
      MakeObject("Even more data", InlineBehavior::PREVENT),
  };
  std::vector<ObjectIdentifier> stored_identifiers;
  for (auto& data : data_array) {
    auto object_identifier = TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(object_identifier, false));
    stored_identifiers.push_back(object_identifier);
  }

  std::vector<CommitId> commits;

  // Add one key-value pair per commit.
  for (const auto& data_identifier : stored_identifiers) {
    std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
    journal->Put(RandomString(environment_.random(), 10), data_identifier, KeyPriority::LAZY);
    EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
    commits.push_back(GetFirstHead()->GetId());
  }

  // List all objects appearing in the commits.
  // There is not enough data in a commit for a tree node to be split, but it may still contain
  // multiple tree nodes. The values will not be split either, so all the objects are chunks and
  // |GetObjectIdentifiers| returns all the piece identifiers that are part of the commits.
  bool called;
  Status status;
  std::set<ObjectIdentifier> object_identifiers;
  for (size_t i = 0; i < commits.size(); i++) {
    std::set<ObjectIdentifier> commit_object_identifiers;
    btree::GetObjectIdentifiers(
        environment_.coroutine_service(), storage_.get(),
        {GetCommit(commits[i])->GetRootIdentifier(), PageStorage::Location::Local()},
        callback::Capture(callback::SetWhenCalled(&called), &status, &commit_object_identifiers));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // We expect all identifiers to be chunks, and either they are tree nodes or they are one of
    // the data identifiers. One of them should be the root of the commit.
    EXPECT_THAT(commit_object_identifiers, Contains(GetCommit(commits[i])->GetRootIdentifier()));
    for (const auto& object_identifier : commit_object_identifiers) {
      EXPECT_TRUE(GetObjectDigestInfo(object_identifier.object_digest()).is_chunk());
      bool is_tree = GetObjectDigestInfo(object_identifier.object_digest()).object_type ==
                     ObjectType::TREE_NODE;
      if (!is_tree) {
        // Commit |i| contains data from data_array[i] as well as data from the previous commits
        // since each is built on top of the previous one.
        EXPECT_THAT(object_identifier,
                    AnyOfArray(stored_identifiers.begin(), stored_identifiers.begin() + i + 1));
      }
      object_identifiers.insert(object_identifier);
    }
  }

  // GetUnsyncedPieces should return the ids of all the objects appearing in the tree of the 3
  // commits.
  std::vector<ObjectIdentifier> returned_object_identifiers;
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &returned_object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(returned_object_identifiers, UnorderedElementsAreArray(object_identifiers));

  // Mark the 2nd object as synced. We now expect to still find the other unsynced objects.
  storage_->MarkPieceSynced(stored_identifiers[1],
                            callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  object_identifiers.erase(stored_identifiers[1]);

  returned_object_identifiers.clear();
  storage_->GetUnsyncedPieces(
      callback::Capture(callback::SetWhenCalled(&called), &status, &returned_object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(returned_object_identifiers, UnorderedElementsAreArray(object_identifiers));
}

TEST_F(PageStorageTest, PageIsSynced) {
  ObjectData data_array[] = {
      MakeObject("Some data", InlineBehavior::PREVENT),
      MakeObject("Some more data", InlineBehavior::PREVENT),
      MakeObject("Even more data", InlineBehavior::PREVENT),
  };
  std::vector<ObjectIdentifier> stored_identifiers;
  for (auto& data : data_array) {
    auto object_identifier = TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    EXPECT_TRUE(IsPieceSynced(object_identifier, false));
    stored_identifiers.push_back(object_identifier);
  }

  // The objects have not been added in a commit: there is nothing to sync and
  // the page is considered synced.
  bool called;
  Status status;
  bool is_synced;
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(is_synced, true);

  // Add all objects in one commit.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  for (const auto& data_identifier : stored_identifiers) {
    journal->Put(RandomString(environment_.random(), 10), data_identifier, KeyPriority::LAZY);
  }
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  CommitId commit_id = GetFirstHead()->GetId();

  // After commiting, the page is unsynced.
  called = false;
  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(is_synced);

  // Mark all objects (tree nodes and values of entries in the tree) as synced and expect that the
  // page is still unsynced.
  //
  // There is not enough data in the tree for a tree node to be split, but it may still contain
  // multiple tree nodes. The values will not be split either, so all the objects are chunks and
  // |GetObjectIdentifiers| returns all the object identifiers we need to mark as synced.
  std::set<ObjectIdentifier> object_identifiers;
  btree::GetObjectIdentifiers(
      environment_.coroutine_service(), storage_.get(),
      {GetFirstHead()->GetRootIdentifier(), PageStorage::Location::Local()},
      callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifiers));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  for (const auto& object_identifier : object_identifiers) {
    ASSERT_TRUE(GetObjectDigestInfo(object_identifier.object_digest()).is_chunk());
    storage_->MarkPieceSynced(object_identifier,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
  }

  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(is_synced);

  // Mark the commit as synced and expect that the page is synced.
  storage_->MarkCommitSynced(commit_id,
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  storage_->IsSynced(callback::Capture(callback::SetWhenCalled(&called), &status, &is_synced));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(is_synced);

  // All objects should be synced now.
  for (const auto& object_identifier : stored_identifiers) {
    EXPECT_TRUE(IsPieceSynced(object_identifier, true));
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
  EXPECT_EQ(status, Status::OK);
  for (ObjectIdentifier& object_identifier : object_identifiers) {
    storage_->MarkPieceSynced(object_identifier,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
  }
  EXPECT_FALSE(storage_->IsOnline());

  // Mark the commit as synced. The page should now be marked as online.
  storage_->MarkCommitSynced(commit->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
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
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(storage_->IsOnline());
}

TEST_F(PageStorageTest, PageIsEmpty) {
  bool called;
  Status status;
  bool is_empty;

  // Initially the page is empty.
  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(is_empty);

  // Add an entry and expect that the page is not empty any more.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::LAZY);
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_FALSE(is_empty);

  // Clear the page and expect it to be empty again.
  journal = storage_->StartCommit(GetFirstHead());
  journal->Delete("key");
  EXPECT_TRUE(TryCommitJournal(std::move(journal), Status::OK));

  storage_->IsEmpty(callback::Capture(callback::SetWhenCalled(&called), &status, &is_empty));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(is_empty);
}

TEST_F(PageStorageTest, UntrackedObjectsSimple) {
  ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);

  // The object is not yet created and its id should not be marked as untracked.
  EXPECT_TRUE(ObjectIsUntracked(data.object_identifier, false));

  // After creating the object it should be marked as untracked.
  ObjectIdentifier object_identifier = TryAddFromLocal(data.value, data.object_identifier);
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));

  // After adding the object in a commit it should not be untracked any more.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", object_identifier, KeyPriority::EAGER);
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(object_identifier, false));
}

TEST_F(PageStorageTest, UntrackedObjectsComplex) {
  ObjectData data_array[] = {
      MakeObject("Some data", InlineBehavior::PREVENT),
      MakeObject("Some more data", InlineBehavior::PREVENT),
      MakeObject("Even more data", InlineBehavior::PREVENT),
  };
  std::vector<ObjectIdentifier> stored_identifiers;
  for (auto& data : data_array) {
    auto object_identifier = TryAddFromLocal(data.value, data.object_identifier);
    EXPECT_TRUE(ObjectIsUntracked(object_identifier, true));
    stored_identifiers.push_back(object_identifier);
  }

  // Add a first commit containing stored_identifiers[0].
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key0", stored_identifiers[0], KeyPriority::LAZY);
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[0], true));
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[0], false));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[1], true));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[2], true));

  // Create a second commit. After calling Put for "key1" for the second time
  // stored_identifiers[1] is no longer part of this commit: it should remain
  // untracked after committing.
  journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key1", stored_identifiers[1], KeyPriority::LAZY);
  journal->Put("key2", stored_identifiers[2], KeyPriority::LAZY);
  journal->Put("key1", stored_identifiers[2], KeyPriority::LAZY);
  journal->Put("key3", stored_identifiers[0], KeyPriority::LAZY);
  ASSERT_TRUE(TryCommitJournal(std::move(journal), Status::OK));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[0], false));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[1], true));
  EXPECT_TRUE(ObjectIsUntracked(stored_identifiers[2], false));
}

TEST_F(PageStorageTest, CommitWatchers) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  // Add a watcher and receive the commit.
  auto expected = TryCommitFromLocal(10);
  ASSERT_TRUE(expected);
  EXPECT_EQ(watcher.commit_count, 1);
  EXPECT_EQ(watcher.last_commit_id, expected->GetId());
  EXPECT_EQ(watcher.last_source, ChangeSource::LOCAL);

  // Add a second watcher.
  FakeCommitWatcher watcher2;
  storage_->AddCommitWatcher(&watcher2);
  expected = TryCommitFromLocal(10);
  ASSERT_TRUE(expected);
  EXPECT_EQ(watcher.commit_count, 2);
  EXPECT_EQ(watcher.last_commit_id, expected->GetId());
  EXPECT_EQ(watcher.last_source, ChangeSource::LOCAL);
  EXPECT_EQ(watcher2.commit_count, 1);
  EXPECT_EQ(watcher2.last_commit_id, expected->GetId());
  EXPECT_EQ(watcher2.last_source, ChangeSource::LOCAL);

  // Remove one watcher.
  storage_->RemoveCommitWatcher(&watcher2);
  expected = TryCommitFromSync();
  EXPECT_EQ(watcher.commit_count, 3);
  EXPECT_EQ(watcher.last_commit_id, expected->GetId());
  EXPECT_EQ(watcher.last_source, ChangeSource::CLOUD);
  EXPECT_EQ(watcher2.commit_count, 1);
}

// If a commit fails to be persisted on disk, no notification should be sent.
TEST_F(PageStorageTest, CommitFailNoWatchNotification) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);
  EXPECT_EQ(watcher.commit_count, 0);

  // Create the commit.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key1", RandomObjectIdentifier(), KeyPriority::EAGER);

  leveldb_->SetFailBatchExecuteAfter(1);
  std::unique_ptr<const Commit> commit = TryCommitJournal(std::move(journal), Status::IO_ERROR);

  // The watcher is not called.
  EXPECT_EQ(watcher.commit_count, 0);
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
    EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);

    storage_->SetSyncMetadata(key, value,
                              callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    storage_->GetSyncMetadata(
        key, callback::Capture(callback::SetWhenCalled(&called), &status, &returned_value));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(returned_value, value);
  }
}

class PageStorageTestAddMultipleCommits : public PageStorageTest {
 public:
  void SetUp() override {
    PageStorageTest::SetUp();
    RunInCoroutine([this](CoroutineHandler* handler) {
      storage_->SetSyncDelegate(&sync_);

      // Build the commit Tree with:
      //   0   1
      //    \ / \
      //     2   3
      //         |
      //         4
      // 0 and 1 are present locally as commits, but their tree is not.
      // 2, 3 and 4 are added as a single batch.
      // A merge of 0 and 1 (commit 5) is used to make the objects of 0 and 1 garbage collectable.
      // Each commit contains one tree node, one eager value and one lazy value.
      tree_object_identifiers_.resize(6);
      eager_object_identifiers_.resize(6);
      lazy_object_identifiers_.resize(6);
      std::vector<std::vector<Entry>> all_entries;
      for (size_t i = 0; i < tree_object_identifiers_.size(); ++i) {
        ObjectData eager_value =
            MakeObject("eager value" + std::to_string(i), InlineBehavior::PREVENT);
        ObjectData lazy_value =
            MakeObject("lazy value" + std::to_string(i), InlineBehavior::PREVENT);
        std::vector<Entry> entries = {
            Entry{"key" + std::to_string(i), eager_value.object_identifier, KeyPriority::EAGER,
                  EntryId("id" + std::to_string(i))},
            Entry{"lazy" + std::to_string(i), lazy_value.object_identifier, KeyPriority::LAZY,
                  EntryId("id" + std::to_string(i))}};
        std::unique_ptr<const btree::TreeNode> node;
        ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
        tree_object_identifiers_[i] = node->GetIdentifier();
        eager_object_identifiers_[i] = eager_value.object_identifier;
        lazy_object_identifiers_[i] = lazy_value.object_identifier;
        sync_.AddObject(eager_value.object_identifier, eager_value.value,
                        ObjectAvailability::P2P_AND_CLOUD);
        sync_.AddObject(lazy_value.object_identifier, lazy_value.value,
                        ObjectAvailability::P2P_AND_CLOUD);
        std::unique_ptr<const Object> root_object =
            TryGetObject(tree_object_identifiers_[i], PageStorage::Location::Local());
        fxl::StringView root_data;
        ASSERT_EQ(root_object->GetData(&root_data), Status::OK);
        sync_.AddObject(tree_object_identifiers_[i], root_data.ToString(),
                        ObjectAvailability::P2P_AND_CLOUD);
        all_entries.push_back(entries);
      }

      // Create the commits, the initial upload batch, and the second batch.
      std::unique_ptr<const Commit> root = GetFirstHead();

      std::vector<std::unique_ptr<const Commit>> parent;
      parent.emplace_back(root->Clone());
      std::unique_ptr<const Commit> commit0 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[0],
          std::move(parent));
      parent.clear();
      sync_.AddDiff(commit0->GetId(), root->GetId(),
                    {{all_entries[0][0], false}, {all_entries[0][1], false}});

      parent.emplace_back(root->Clone());
      std::unique_ptr<const Commit> commit1 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[1],
          std::move(parent));
      parent.clear();
      sync_.AddDiff(commit1->GetId(), root->GetId(),
                    {{all_entries[1][0], false}, {all_entries[1][1], false}});

      // Ensure that commit0 has a larger id than commit1.
      if (commit0->GetId() < commit1->GetId()) {
        std::swap(commit0, commit1);
        std::swap(tree_object_identifiers_[0], tree_object_identifiers_[1]);
        std::swap(eager_object_identifiers_[0], eager_object_identifiers_[1]);
        std::swap(lazy_object_identifiers_[0], lazy_object_identifiers_[1]);
      }
      commit_identifiers_.push_back(commit0->GetId());
      commit_identifiers_.push_back(commit1->GetId());

      parent.emplace_back(commit0->Clone());
      parent.emplace_back(commit1->Clone());
      std::unique_ptr<const Commit> commit2 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[2],
          std::move(parent));
      parent.clear();
      commit_identifiers_.push_back(commit2->GetId());
      EXPECT_EQ(commit2->GetParentIds()[0], commit1->GetId());

      parent.emplace_back(commit1->Clone());
      std::unique_ptr<const Commit> commit3 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[3],
          std::move(parent));
      commit_identifiers_.push_back(commit3->GetId());
      parent.clear();

      parent.emplace_back(commit3->Clone());
      std::unique_ptr<const Commit> commit4 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[4],
          std::move(parent));
      commit_identifiers_.push_back(commit4->GetId());
      parent.clear();

      parent.emplace_back(commit0->Clone());
      parent.emplace_back(commit1->Clone());
      std::unique_ptr<const Commit> commit5 = storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), tree_object_identifiers_[5],
          std::move(parent));
      commit_identifiers_.push_back(commit5->GetId());
      parent.clear();
      sync_.AddDiff(commit5->GetId(), commit1->GetId(),
                    {{all_entries[1][0], true},
                     {all_entries[1][1], true},
                     {all_entries[5][0], false},
                     {all_entries[5][1], false}});

      // Commit 5 is the only commit that is guaranteed not to be GCed after 0, 1 and 5 are added.
      sync_.AddDiff(commit2->GetId(), commit5->GetId(),
                    {{all_entries[5][0], true},
                     {all_entries[5][1], true},
                     {all_entries[2][0], false},
                     {all_entries[2][1], false}});
      sync_.AddDiff(commit3->GetId(), commit5->GetId(),
                    {{all_entries[5][0], true},
                     {all_entries[5][1], true},
                     {all_entries[3][0], false},
                     {all_entries[3][1], false}});
      sync_.AddDiff(commit4->GetId(), commit5->GetId(),
                    {{all_entries[5][0], true},
                     {all_entries[5][1], true},
                     {all_entries[4][0], false},
                     {all_entries[4][1], false}});

      std::vector<PageStorage::CommitIdAndBytes> initial_batch;
      initial_batch.emplace_back(commit0->GetId(), commit0->GetStorageBytes().ToString());
      initial_batch.emplace_back(commit1->GetId(), commit1->GetStorageBytes().ToString());
      initial_batch.emplace_back(commit5->GetId(), commit5->GetStorageBytes().ToString());

      test_batch_.emplace_back(commit2->GetId(), commit2->GetStorageBytes().ToString());
      test_batch_.emplace_back(commit3->GetId(), commit3->GetStorageBytes().ToString());
      test_batch_.emplace_back(commit4->GetId(), commit4->GetStorageBytes().ToString());

      commit0.reset();
      commit1.reset();
      commit2.reset();
      commit3.reset();
      commit4.reset();
      commit5.reset();

      // Reset and clear the storage. We do not retrack the identifiers immediately because we
      // want to leave the opportunity for the roots of commit 0 and 1 to be collected.
      ResetStorage();
      storage_->SetSyncDelegate(&sync_);

      // Add commits 0, 1 and 5 from the cloud and let garbage collection kick in.
      bool called;
      Status status;
      storage_->AddCommitsFromSync(std::move(initial_batch), ChangeSource::CLOUD,
                                   callback::Capture(callback::SetWhenCalled(&called), &status));
      RunLoopUntilIdle();
      ASSERT_TRUE(called);
      EXPECT_EQ(status, Status::OK);

      // Check that the objects of commits 0 and 1 have been collected.
      for (auto& identifiers :
           {tree_object_identifiers_, eager_object_identifiers_, lazy_object_identifiers_}) {
        for (auto commit : {0, 1}) {
          ObjectIdentifier identifier = identifiers[commit];
          RetrackIdentifier(&identifier);
          std::unique_ptr<const Piece> piece;
          ASSERT_EQ(ReadObject(handler, identifier, &piece), Status::INTERNAL_NOT_FOUND);
        }
      }

      // Retrack all identifiers.
      for (auto object_identifiers :
           {&tree_object_identifiers_, &eager_object_identifiers_, &lazy_object_identifiers_}) {
        for (auto& identifier : *object_identifiers) {
          RetrackIdentifier(&identifier);
        }
      }
      sync_.object_requests.clear();
      sync_.diff_requests.clear();
    });
  }

  FakeSyncDelegate sync_;
  std::vector<CommitId> commit_identifiers_;
  std::vector<ObjectIdentifier> tree_object_identifiers_;
  std::vector<ObjectIdentifier> eager_object_identifiers_;
  std::vector<ObjectIdentifier> lazy_object_identifiers_;
  std::vector<PageStorage::CommitIdAndBytes> test_batch_;
};

TEST_F(PageStorageTestAddMultipleCommits, FromCloudNoDiff) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    sync_.SetSyncFeatures(
        {HasP2P::NO, HasCloud::YES_NO_DIFFS, DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES});
    // Add commits 2, 3 4.
    bool called;
    Status status;

    storage_->AddCommitsFromSync(std::move(test_batch_), ChangeSource::CLOUD,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Diffs for commits 2 and 4 have been requested (but not returned).
    EXPECT_THAT(sync_.diff_requests, UnorderedElementsAre(Pair(commit_identifiers_[2], _),
                                                          Pair(commit_identifiers_[4], _)));

    // The tree and eager objects of commits 2 and 4 have been requested.
    // The tree has been first requested as a tree node (and received no response), then as a
    // blob.
    EXPECT_THAT(
        sync_.object_requests,
        UnorderedElementsAre(Pair(tree_object_identifiers_[2], RetrievedObjectType::TREE_NODE),
                             Pair(tree_object_identifiers_[2], RetrievedObjectType::BLOB),
                             Pair(eager_object_identifiers_[2], RetrievedObjectType::BLOB),
                             Pair(tree_object_identifiers_[4], RetrievedObjectType::TREE_NODE),
                             Pair(tree_object_identifiers_[4], RetrievedObjectType::BLOB),
                             Pair(eager_object_identifiers_[4], RetrievedObjectType::BLOB)));
  });
}

TEST_F(PageStorageTestAddMultipleCommits, FromCloudWithDiff) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    sync_.SetSyncFeatures(
        {HasP2P::NO, HasCloud::YES_WITH_DIFFS, DiffCompatibilityPolicy::USE_ONLY_DIFFS});
    // Add commits 2, 3 4.
    bool called;
    Status status;

    storage_->AddCommitsFromSync(std::move(test_batch_), ChangeSource::CLOUD,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // Commits 0 and 2 have been requested.
    EXPECT_THAT(sync_.diff_requests, UnorderedElementsAre(Pair(commit_identifiers_[2], _),
                                                          Pair(commit_identifiers_[4], _)));
    // The tree and eager objects of commit 2 and 4 have been requested. The tree has been
    // requested as a TREE_NODE, but not as a BLOB, as a diff has been received.
    EXPECT_THAT(
        sync_.object_requests,
        UnorderedElementsAre(Pair(tree_object_identifiers_[2], RetrievedObjectType::TREE_NODE),
                             Pair(eager_object_identifiers_[2], RetrievedObjectType::BLOB),
                             Pair(tree_object_identifiers_[4], RetrievedObjectType::TREE_NODE),
                             Pair(eager_object_identifiers_[4], RetrievedObjectType::BLOB)));
  });
}

TEST_F(PageStorageTestAddMultipleCommits, FromP2P) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    sync_.SetSyncFeatures({HasP2P::YES, HasCloud::NO, DiffCompatibilityPolicy::USE_ONLY_DIFFS});

    // Add commits 2, 3 4.
    bool called;
    Status status;
    storage_->AddCommitsFromSync(std::move(test_batch_), ChangeSource::P2P,
                                 callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    // The tree, and eager objects of all new commits have been downloaded, as well as the tree
    // of parent commits.
    EXPECT_THAT(
        sync_.object_requests,
        UnorderedElementsAre(Pair(tree_object_identifiers_[1], RetrievedObjectType::TREE_NODE),
                             Pair(tree_object_identifiers_[2], RetrievedObjectType::TREE_NODE),
                             Pair(eager_object_identifiers_[2], RetrievedObjectType::BLOB),
                             Pair(tree_object_identifiers_[3], RetrievedObjectType::TREE_NODE),
                             Pair(eager_object_identifiers_[3], RetrievedObjectType::BLOB),
                             Pair(tree_object_identifiers_[4], RetrievedObjectType::TREE_NODE),
                             Pair(eager_object_identifiers_[4], RetrievedObjectType::BLOB)));

    // They have also been requested as diffs.
    EXPECT_THAT(
        sync_.diff_requests,
        UnorderedElementsAre(Pair(commit_identifiers_[1], _), Pair(commit_identifiers_[2], _),
                             Pair(commit_identifiers_[3], _), Pair(commit_identifiers_[4], _)));
  });
}

TEST_F(PageStorageTest, Generation) {
  std::unique_ptr<const Commit> commit1 = TryCommitFromLocal(3);
  ASSERT_TRUE(commit1);
  EXPECT_EQ(commit1->GetGeneration(), 1u);

  std::unique_ptr<const Commit> commit2 = TryCommitFromLocal(3);
  ASSERT_TRUE(commit2);
  EXPECT_EQ(commit2->GetGeneration(), 2u);

  std::unique_ptr<Journal> journal =
      storage_->StartMergeCommit(std::move(commit1), std::move(commit2));

  std::unique_ptr<const Commit> commit3 = TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(commit3);
  EXPECT_EQ(commit3->GetGeneration(), 3u);
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
  ASSERT_EQ(status, Status::KEY_NOT_FOUND);

  for (int i = 0; i < size; ++i) {
    std::string expected_key = absl::StrFormat("key%05d", i);
    storage_->GetEntryFromCommit(
        *commit, expected_key,
        callback::Capture(callback::SetWhenCalled(&called), &status, &entry));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
    EXPECT_EQ(entry.key, expected_key);
  }
}

TEST_F(PageStorageTest, GetDiffForCloudInsertion) {
  // Create an initial commit with 10 keys and then another one having commit1 as a parent,
  // inserting a new key.
  std::unique_ptr<const Commit> commit1 = TryCommitFromLocal(10);
  ASSERT_TRUE(commit1);

  const std::string new_key = "new_key";
  const ObjectIdentifier new_identifier = RandomObjectIdentifier();
  const KeyPriority new_priority = KeyPriority::LAZY;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put(new_key, new_identifier, new_priority);
  std::unique_ptr<const Commit> commit2 = TryCommitJournal(std::move(journal), Status::OK);

  bool called = false;
  storage_->GetDiffForCloud(
      *commit2, [&](Status status, CommitIdView base_id, std::vector<EntryChange> changes) {
        called = true;
        ASSERT_EQ(status, Status::OK);
        EXPECT_EQ(base_id, commit1->GetId());

        EXPECT_THAT(changes, SizeIs(1));
        EXPECT_EQ(changes[0].entry.key, new_key);
        EXPECT_EQ(changes[0].entry.object_identifier, new_identifier);
        EXPECT_EQ(changes[0].entry.priority, new_priority);
        EXPECT_THAT(changes[0].entry.entry_id, Not(IsEmpty()));
        EXPECT_EQ(changes[0].deleted, false);
      });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
}

TEST_F(PageStorageTest, GetDiffForCloudDeletion) {
  // Create an initial commit with 3 keys and then another one having commit1 as a parent,
  // deleting a key.
  const std::string deleted_key = "deleted_key";
  const ObjectIdentifier deleted_identifier = RandomObjectIdentifier();
  const KeyPriority deleted_priority = KeyPriority::EAGER;

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("a key", RandomObjectIdentifier(), KeyPriority::LAZY);
  journal->Put(deleted_key, deleted_identifier, deleted_priority);
  journal->Put("last key", RandomObjectIdentifier(), KeyPriority::LAZY);
  std::unique_ptr<const Commit> commit1 = TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(commit1);

  journal = storage_->StartCommit(GetFirstHead());
  journal->Delete(deleted_key);
  std::unique_ptr<const Commit> commit2 = TryCommitJournal(std::move(journal), Status::OK);

  bool called = false;
  storage_->GetDiffForCloud(
      *commit2, [&](Status status, CommitIdView base_id, std::vector<EntryChange> changes) {
        called = true;
        ASSERT_EQ(status, Status::OK);
        EXPECT_EQ(base_id, commit1->GetId());

        EXPECT_THAT(changes, SizeIs(1));
        EXPECT_EQ(changes[0].entry.key, deleted_key);
        EXPECT_EQ(changes[0].entry.object_identifier, deleted_identifier);
        EXPECT_EQ(changes[0].entry.priority, deleted_priority);
        EXPECT_THAT(changes[0].entry.entry_id, Not(IsEmpty()));
        EXPECT_EQ(changes[0].deleted, true);
      });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
}

TEST_F(PageStorageTest, GetDiffForCloudUpdate) {
  // Create an initial commit with 3 keys and then another one having commit1 as a parent,
  // updating a key.
  const std::string updated_key = "updated_key";
  const ObjectIdentifier old_identifier = RandomObjectIdentifier();
  const KeyPriority old_priority = KeyPriority::LAZY;
  const ObjectIdentifier new_identifier = RandomObjectIdentifier();
  const KeyPriority new_priority = KeyPriority::EAGER;

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("a key", RandomObjectIdentifier(), KeyPriority::LAZY);
  journal->Put(updated_key, old_identifier, old_priority);
  journal->Put("last key", RandomObjectIdentifier(), KeyPriority::LAZY);
  std::unique_ptr<const Commit> commit1 = TryCommitJournal(std::move(journal), Status::OK);
  ASSERT_TRUE(commit1);

  journal = storage_->StartCommit(GetFirstHead());
  journal->Put(updated_key, new_identifier, new_priority);
  std::unique_ptr<const Commit> commit2 = TryCommitJournal(std::move(journal), Status::OK);

  bool called = false;
  storage_->GetDiffForCloud(
      *commit2, [&](Status status, CommitIdView base_id, std::vector<EntryChange> changes) {
        called = true;
        ASSERT_EQ(status, Status::OK);
        EXPECT_EQ(base_id, commit1->GetId());

        EXPECT_THAT(changes, SizeIs(2));
        EXPECT_EQ(changes[0].entry.key, updated_key);
        EXPECT_EQ(changes[0].entry.object_identifier, old_identifier);
        EXPECT_EQ(changes[0].entry.priority, old_priority);
        EXPECT_EQ(changes[0].deleted, true);

        EXPECT_EQ(changes[1].entry.key, updated_key);
        EXPECT_EQ(changes[1].entry.object_identifier, new_identifier);
        EXPECT_EQ(changes[1].entry.priority, new_priority);
        EXPECT_EQ(changes[1].deleted, false);
      });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
}

TEST_F(PageStorageTest, GetDiffForCloudEntryIdCorrectness) {
  // Create an initial commit with 10 keys, then one having commit1 as a parent adding a key and
  // then one having commit2 as a parent deleting the same key.
  std::unique_ptr<const Commit> commit1 = TryCommitFromLocal(10);
  ASSERT_TRUE(commit1);

  const std::string new_key = "new_key";
  const ObjectIdentifier new_identifier = RandomObjectIdentifier();
  const KeyPriority new_priority = KeyPriority::LAZY;

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put(new_key, new_identifier, new_priority);
  std::unique_ptr<const Commit> commit2 = TryCommitJournal(std::move(journal), Status::OK);

  journal = storage_->StartCommit(GetFirstHead());
  journal->Delete(new_key);
  std::unique_ptr<const Commit> commit3 = TryCommitJournal(std::move(journal), Status::OK);

  // The entry_id of the inserted entry should be the same as the entry_id of the deleted one.
  EntryId expected_entry_id;
  bool called = false;
  storage_->GetDiffForCloud(
      *commit2, [&](Status status, CommitIdView base_id, std::vector<EntryChange> changes) {
        called = true;
        ASSERT_EQ(status, Status::OK);
        EXPECT_EQ(base_id, commit1->GetId());

        EXPECT_THAT(changes, SizeIs(1));
        EXPECT_EQ(changes[0].entry.key, new_key);
        EXPECT_EQ(changes[0].entry.object_identifier, new_identifier);
        EXPECT_EQ(changes[0].entry.priority, new_priority);
        EXPECT_THAT(changes[0].entry.entry_id, Not(IsEmpty()));
        EXPECT_EQ(changes[0].deleted, false);
        expected_entry_id = changes[0].entry.entry_id;
      });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  called = false;
  storage_->GetDiffForCloud(
      *commit3, [&](Status status, CommitIdView base_id, std::vector<EntryChange> changes) {
        called = true;
        ASSERT_EQ(status, Status::OK);
        EXPECT_EQ(base_id, commit2->GetId());

        EXPECT_THAT(changes, SizeIs(1));
        EXPECT_EQ(changes[0].entry.key, new_key);
        EXPECT_EQ(changes[0].entry.object_identifier, new_identifier);
        EXPECT_EQ(changes[0].entry.priority, new_priority);
        EXPECT_THAT(changes[0].entry.entry_id, Not(IsEmpty()));
        EXPECT_EQ(changes[0].deleted, true);

        EXPECT_EQ(expected_entry_id, changes[0].entry.entry_id);
      });
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
}

TEST_F(PageStorageTest, WatcherForReEntrantCommits) {
  FakeCommitWatcher watcher;
  storage_->AddCommitWatcher(&watcher);

  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(std::move(commit1));
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  EXPECT_EQ(watcher.commit_count, 2);
  EXPECT_EQ(watcher.last_commit_id, commit2->GetId());
}

TEST_F(PageStorageTest, NoOpCommit) {
  std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
  ASSERT_FALSE(heads.empty());

  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());

  // Create a key, and delete it.
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
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
  ASSERT_EQ(status, Status::OK);
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

  ObjectIdentifier value_1 = RandomInlineObjectIdentifier();
  ObjectIdentifier value_2 = RandomInlineObjectIdentifier();
  ObjectIdentifier merge_value = RandomInlineObjectIdentifier();
  ASSERT_NE(value_1, value_2);

  std::unique_ptr<Journal> journal1 = storage_->StartCommit(base_commit->Clone());
  journal1->Put("key", value_1, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit1;
  storage_->CommitJournal(std::move(journal1),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  RunLoopFor(zx::sec(1));

  std::unique_ptr<Journal> journal2 = storage_->StartCommit(base_commit->Clone());
  journal2->Put("key", value_2, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit2;
  storage_->CommitJournal(std::move(journal2),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // Create a merge.
  std::unique_ptr<Journal> journal3 =
      storage_->StartMergeCommit(commit1->Clone(), commit2->Clone());
  journal3->Put("key", merge_value, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit3;
  storage_->CommitJournal(std::move(journal3),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit3));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

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
  RetrackIdentifier(&merge_value);

  // This does not need diffs.
  FakeSyncDelegate sync(SyncFeatures::kNoDiff);
  storage_->SetSyncDelegate(&sync);
  for (const auto& data : object_data_base) {
    sync.AddObject(data.first, data.second, ObjectAvailability::P2P_AND_CLOUD);
  }

  // Start adding the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  storage_->AddCommitsFromSync(std::move(commits_and_bytes_base), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(commits_from_sync_status, Status::OK);
  ASSERT_EQ(GetHeads().size(), 2u);

  std::vector<fit::closure> sync_delegate_calls;
  DelayingFakeSyncDelegate sync2(
      [&sync_delegate_calls](fit::closure callback) {
        sync_delegate_calls.push_back(std::move(callback));
      },
      [](auto callback) { callback(); }, SyncFeatures::kNoDiff);
  storage_->SetSyncDelegate(&sync2);

  for (const auto& data : object_data_merge) {
    sync2.AddObject(data.first, data.second, ObjectAvailability::P2P_AND_CLOUD);
  }
  storage_->AddCommitsFromSync(std::move(commits_and_bytes_merge), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status));

  // Make the loop run until GetObject is called in sync, and before
  // AddCommitsFromSync finishes.
  RunLoopUntilIdle();
  EXPECT_THAT(sync_delegate_calls, Not(IsEmpty()));
  EXPECT_FALSE(commits_from_sync_called);

  // Add the local commit.
  auto heads = GetHeads();
  Status commits_from_local_status;
  std::unique_ptr<Journal> journal =
      storage_->StartMergeCommit(std::move(heads[0]), std::move(heads[1]));
  journal->Put("key", merge_value, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(
      std::move(journal),
      callback::Capture(callback::SetWhenCalled(&called), &commits_from_local_status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(commits_from_local_status, Status::OK);
  EXPECT_FALSE(commits_from_sync_called);

  EXPECT_EQ(commit->GetId(), id3);

  // The local commit should be commited.
  for (auto& callback : sync_delegate_calls) {
    callback();
  }

  // Let the two AddCommit finish.
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(commits_from_sync_status, Status::OK);
  EXPECT_EQ(commits_from_local_status, Status::OK);

  // Verify that the commit is added correctly.
  storage_->GetCommit(id3, callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // The commit should be marked as synced.
  EXPECT_EQ(GetUnsyncedCommits().size(), 0u);
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
  journal_a->Put("a", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_a = TryCommitJournal(std::move(journal_a), Status::OK);
  ASSERT_TRUE(commit_a);
  EXPECT_EQ(commit_a->GetGeneration(), 1u);

  std::unique_ptr<Journal> journal_b = storage_->StartCommit(root->Clone());
  journal_b->Put("b", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_b = TryCommitJournal(std::move(journal_b), Status::OK);
  ASSERT_TRUE(commit_b);
  EXPECT_EQ(commit_b->GetGeneration(), 1u);

  std::unique_ptr<Journal> journal_merge =
      storage_->StartMergeCommit(std::move(commit_a), std::move(commit_b));

  std::unique_ptr<const Commit> commit_merge =
      TryCommitJournal(std::move(journal_merge), Status::OK);
  ASSERT_TRUE(commit_merge);
  EXPECT_EQ(commit_merge->GetGeneration(), 2u);

  std::unique_ptr<Journal> journal_c = storage_->StartCommit(std::move(root));
  journal_c->Put("c", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_c = TryCommitJournal(std::move(journal_c), Status::OK);
  ASSERT_TRUE(commit_c);
  EXPECT_EQ(commit_c->GetGeneration(), 1u);

  // Verify that the merge commit is returned as last, even though commit C is
  // older.
  std::vector<std::unique_ptr<const Commit>> unsynced_commits = GetUnsyncedCommits();
  EXPECT_EQ(unsynced_commits.size(), 4u);
  EXPECT_EQ(unsynced_commits.back()->GetId(), commit_merge->GetId());
  EXPECT_LT(commit_merge->GetTimestamp(), commit_c->GetTimestamp());
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

// Add a commit for which we don't have its parent. Verify that an error is returned.
TEST_F(PageStorageTest, AddCommitsMissingParent) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  std::vector<std::unique_ptr<const Commit>> parent;
  parent.emplace_back(GetFirstHead());
  auto commit_parent = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_identifier, std::move(parent));
  parent.clear();
  parent.push_back(commit_parent->Clone());
  auto commit_child = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_identifier, std::move(parent));

  std::vector<PageStorage::CommitIdAndBytes> commits_and_bytes;
  commits_and_bytes.emplace_back(commit_child->GetId(), commit_child->GetStorageBytes().ToString());

  bool called;
  Status status;
  storage_->AddCommitsFromSync(std::move(commits_and_bytes), ChangeSource::P2P,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);
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
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);

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

TEST_F(PageStorageTest, GetCommitRootIdentifier) {
  bool called;
  Status status;
  std::unique_ptr<const Commit> base_commit = GetFirstHead();
  std::unique_ptr<Journal> journal = storage_->StartCommit(base_commit->Clone());
  ObjectIdentifier value = RandomInlineObjectIdentifier();
  journal->Put("key", value, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(commit);

  ObjectIdentifier root_id = commit->GetRootIdentifier();
  std::string root_data = TryGetPiece(root_id)->GetData().ToString();
  CommitId commit_id = commit->GetId();
  std::string commit_data = commit->GetStorageBytes().ToString();
  auto commit_id_and_bytes = CommitAndBytesFromCommit(*commit);

  commit.reset();
  ResetStorage();

  bool sync_delegate_called;
  fit::closure sync_delegate_call;
  DelayingFakeSyncDelegate sync(
      callback::Capture(callback::SetWhenCalled(&sync_delegate_called), &sync_delegate_call),
      [](auto callback) { callback(); }, SyncFeatures::kNoDiff);
  storage_->SetSyncDelegate(&sync);
  sync.AddObject(root_id, root_data, ObjectAvailability::P2P_AND_CLOUD);

  // Start adding the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  storage_->AddCommitsFromSync(std::move(commit_id_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(commits_from_sync_called);
  EXPECT_TRUE(sync_delegate_called);
  ASSERT_TRUE(sync_delegate_call);

  // AddCommitsFromSync is waiting in GetObject.
  ObjectIdentifier root_id_from_storage;
  PageStorageImplAccessorForTest::GetCommitRootIdentifier(
      storage_, commit_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &root_id_from_storage));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(root_id_from_storage, root_id);

  // Unblock AddCommitsFromSync.
  sync_delegate_call();
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(commits_from_sync_status, Status::OK);

  // The map is empty, and the root identifier is fetched from the database.
  EXPECT_TRUE(PageStorageImplAccessorForTest::RootCommitIdentifierMapIsEmpty(storage_));
  PageStorageImplAccessorForTest::GetCommitRootIdentifier(
      storage_, commit_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &root_id_from_storage));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(root_id_from_storage, root_id);
}

TEST_P(PageStorageSyncTest, GetCommitRootIdentifierFailedToAdd) {
  bool called;
  Status status;
  std::unique_ptr<const Commit> base_commit = GetFirstHead();
  std::unique_ptr<Journal> journal = storage_->StartCommit(base_commit->Clone());
  ObjectIdentifier value = RandomInlineObjectIdentifier();
  journal->Put("key", value, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(commit);

  ObjectIdentifier root_id = commit->GetRootIdentifier();
  std::string root_data = TryGetPiece(root_id)->GetData().ToString();
  CommitId commit_id = commit->GetId();
  std::string commit_data = commit->GetStorageBytes().ToString();
  auto commit_id_and_bytes = CommitAndBytesFromCommit(*commit);

  commit.reset();
  ResetStorage();

  FakeSyncDelegate sync(GetParam());
  storage_->SetSyncDelegate(&sync);
  // We do not add the root object nor the diff: GetObject will fail.

  // Add the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  storage_->AddCommitsFromSync(std::move(commit_id_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(commits_from_sync_status, Status::INTERNAL_NOT_FOUND);

  // The commit id to root identifier mapping is still available.
  ObjectIdentifier root_id_from_storage;
  PageStorageImplAccessorForTest::GetCommitRootIdentifier(
      storage_, commit_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &root_id_from_storage));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(root_id_from_storage, root_id);
}

TEST_F(PageStorageTest, GetCommitIdFromRemoteId) {
  bool called;
  Status status;
  std::unique_ptr<const Commit> base_commit = GetFirstHead();
  std::unique_ptr<Journal> journal = storage_->StartCommit(base_commit->Clone());
  ObjectIdentifier value = RandomInlineObjectIdentifier();
  journal->Put("key", value, KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(commit);

  ObjectIdentifier root_id = commit->GetRootIdentifier();
  std::string root_data = TryGetPiece(root_id)->GetData().ToString();
  CommitId commit_id = commit->GetId();
  std::string commit_data = commit->GetStorageBytes().ToString();
  auto commit_id_and_bytes = CommitAndBytesFromCommit(*commit);
  std::string remote_commit_id = encryption_service_.EncodeCommitId(commit_id);

  ResetStorage();
  EXPECT_TRUE(PageStorageImplAccessorForTest::RemoteCommitIdMapIsEmpty(storage_));

  bool sync_delegate_called;
  fit::closure sync_delegate_call;
  DelayingFakeSyncDelegate sync(
      callback::Capture(callback::SetWhenCalled(&sync_delegate_called), &sync_delegate_call));
  storage_->SetSyncDelegate(&sync);
  sync.AddObject(root_id, root_data, ObjectAvailability::P2P_AND_CLOUD);

  // Start adding the remote commit.
  bool commits_from_sync_called;
  Status commits_from_sync_status;
  storage_->AddCommitsFromSync(std::move(commit_id_and_bytes), ChangeSource::CLOUD,
                               callback::Capture(callback::SetWhenCalled(&commits_from_sync_called),
                                                 &commits_from_sync_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(commits_from_sync_called);
  EXPECT_TRUE(sync_delegate_called);
  ASSERT_TRUE(sync_delegate_call);

  // AddCommitsFromSync is waiting in GetObject.
  CommitId commit_id_from_storage;
  storage_->GetCommitIdFromRemoteId(
      remote_commit_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &commit_id_from_storage));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commit_id_from_storage, commit_id);

  // Unblock AddCommitsFromSync.
  sync_delegate_call();
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_from_sync_called);
  EXPECT_EQ(commits_from_sync_status, Status::OK);

  // The map is empty, and the root identifier is fetched from the database.
  EXPECT_TRUE(PageStorageImplAccessorForTest::RemoteCommitIdMapIsEmpty(storage_));
  storage_->GetCommitIdFromRemoteId(
      remote_commit_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &commit_id_from_storage));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commit_id_from_storage, commit_id);
}

TEST_F(PageStorageTest, ChooseDiffBases) {
  // We have the following commit tree, with the uppercase commits synced and the lowercase
  // commits unsynced.
  //
  //      (root)
  //     /  |  \
  //  (A)  (B)  (C)
  //    \  /     |
  //     (e)    (D)
  //      |
  //     (f)
  //
  // The set of sync heads is {A, B, D}, and they are used as diff bases.
  std::vector<std::unique_ptr<const Commit>> heads = GetHeads();

  // Build the tree.
  bool called;
  Status status;
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_A;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_A));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_B;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_B));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_C;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_C));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(commit_C->Clone());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_D;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_D));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartMergeCommit(commit_A->Clone(), commit_B->Clone());
  std::unique_ptr<const Commit> commit_e;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_e));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  journal = storage_->StartCommit(commit_e->Clone());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_f;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_f));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // Mark A,B,C,D as synced.
  for (auto& commit_id :
       {commit_A->GetId(), commit_B->GetId(), commit_C->GetId(), commit_D->GetId()}) {
    storage_->MarkCommitSynced(commit_id,
                               callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
  }

  // Check that the diff bases are the sync heads.
  std::vector<CommitId> sync_heads;
  // The target commit is ignored.
  CommitId target_id = commit_f->GetId();
  PageStorageImplAccessorForTest::ChooseDiffBases(
      storage_, target_id,
      callback::Capture(callback::SetWhenCalled(&called), &status, &sync_heads));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(sync_heads,
              UnorderedElementsAre(commit_A->GetId(), commit_B->GetId(), commit_D->GetId()));
}

// Checks that the RetrievedObjectType can vary for a given piece depending on why we're reading
// it.
TEST_F(PageStorageTest, GetPieceRetrievedObjectType) {
  // Build a random, valid tree node.
  std::vector<Entry> entries;
  CreateEntries(1000, &entries);
  std::string data_str = btree::EncodeNode(0, entries, {});
  ASSERT_TRUE(btree::CheckValidTreeNodeSerialization(data_str));

  // Split the tree node content into pieces and add them to a SyncDelegate to be
  // retrieved by GetObject.
  FakeSyncDelegate sync(SyncFeatures::kNoDiff);
  std::map<ObjectDigest, ObjectIdentifier> digest_to_identifier;
  ObjectIdentifier object_identifier =
      ForEachPiece(data_str, ObjectType::TREE_NODE, &fake_factory_,
                   [&sync, &digest_to_identifier](std::unique_ptr<const Piece> piece) {
                     ObjectIdentifier piece_identifier = piece->GetIdentifier();
                     if (GetObjectDigestInfo(piece_identifier.object_digest()).is_inlined()) {
                       return;
                     }
                     digest_to_identifier[piece_identifier.object_digest()] = piece_identifier;
                     sync.AddObject(std::move(piece_identifier), piece->GetData().ToString(),
                                    ObjectAvailability::P2P);
                   });
  ASSERT_EQ(GetObjectDigestInfo(object_identifier.object_digest()).piece_type, PieceType::INDEX);
  storage_->SetSyncDelegate(&sync);

  // Get a non-root piece as a value.
  ObjectIdentifier non_root_piece_identifier;
  for (const auto& [digest, identifier] : digest_to_identifier) {
    if (identifier != object_identifier) {
      non_root_piece_identifier = identifier;
      break;
    }
  }
  ASSERT_NE(non_root_piece_identifier, ObjectIdentifier());
  EXPECT_NE(non_root_piece_identifier, object_identifier);
  // We are cheating and re-adding the object as available from both P2P and cloud without its
  // content: this will only update the object availability.
  sync.AddObject(non_root_piece_identifier, "", ObjectAvailability::P2P_AND_CLOUD);

  std::unique_ptr<const Object> value_object =
      TryGetObject(non_root_piece_identifier, PageStorage::Location::ValueFromNetwork());
  EXPECT_THAT(sync.object_requests,
              ElementsAre(Pair(non_root_piece_identifier, RetrievedObjectType::BLOB)));

  // Reset and fetch the whole tree.
  sync.object_requests.clear();
  ResetStorage();
  storage_->SetSyncDelegate(&sync);
  RetrackIdentifier(&non_root_piece_identifier);
  RetrackIdentifier(&object_identifier);

  // Get the tree node containing the non-root piece.
  CommitId dummy_commit = GetFirstHead()->GetId();
  std::unique_ptr<const Object> node_object =
      TryGetObject(object_identifier, PageStorage::Location::TreeNodeFromNetwork(dummy_commit));
  EXPECT_THAT(sync.object_requests,
              Contains(Pair(non_root_piece_identifier, RetrievedObjectType::TREE_NODE)));
  EXPECT_THAT(sync.object_requests,
              Not(Contains(Pair(non_root_piece_identifier, RetrievedObjectType::BLOB))));
}

TEST_F(PageStorageTest, UpdateClock) {
  ResetStorage(CommitPruningPolicy::LOCAL_IMMEDIATE);

  // The clock should be empty;
  bool called;
  Status status;
  Clock clock0;
  storage_->GetClock(callback::Capture(callback::SetWhenCalled(&called), &status, &clock0));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  EXPECT_THAT(clock0, IsEmpty());

  // Build the tree.
  std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_A;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_A));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // The clock should contain one element, and point to the current head commit.
  Clock clock1;
  storage_->GetClock(callback::Capture(callback::SetWhenCalled(&called), &status, &clock1));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  EXPECT_THAT(clock1, ElementsAre(Pair(_, DeviceClockMatchesCommit(*commit_A))));

  // It is updated after a new single head is present

  journal = storage_->StartCommit(std::move(commit_A));
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  std::unique_ptr<const Commit> commit_B;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_B));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // The clock should contain one element, and point to the current head commit.
  Clock clock2;
  storage_->GetClock(callback::Capture(callback::SetWhenCalled(&called), &status, &clock2));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  // The device ID should not have changed.
  const clocks::DeviceId device_id = clock1.begin()->first;
  EXPECT_THAT(clock2, ElementsAre(Pair(device_id, DeviceClockMatchesCommit(*commit_B))));

  // If there is a conflict, no clock update should occur.
  std::unique_ptr<Journal> journal1 = storage_->StartCommit(commit_B->Clone());
  journal1->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);

  std::unique_ptr<Journal> journal2 = storage_->StartCommit(std::move(commit_B));
  journal2->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);

  std::unique_ptr<const Commit> commit_C;
  storage_->CommitJournal(std::move(journal1),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_C));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  std::unique_ptr<const Commit> commit_D;
  storage_->CommitJournal(std::move(journal2),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit_D));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  Clock clock3;
  storage_->GetClock(callback::Capture(callback::SetWhenCalled(&called), &status, &clock3));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  EXPECT_EQ(clock3, clock2);
}

TEST_F(PageStorageTest, GetGenerationAndMissingParents) {
  std::unique_ptr<const btree::TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries({}, {}, &node));
  ObjectIdentifier root_identifier = node->GetIdentifier();

  // Send a commit with two parents, one missing and one not.
  std::vector<std::unique_ptr<const Commit>> parents;
  parents.push_back(GetFirstHead());
  std::unique_ptr<const Commit> missing_parent =
      storage_->GetCommitFactory()->FromContentAndParents(
          environment_.clock(), environment_.random(), root_identifier, std::move(parents));

  parents.clear();
  parents.push_back(GetFirstHead());
  parents.push_back(missing_parent->Clone());
  std::unique_ptr<const Commit> commit_to_add = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_identifier, std::move(parents));

  PageStorage::CommitIdAndBytes id_and_bytes(commit_to_add->GetId(),
                                             commit_to_add->GetStorageBytes().ToString());
  bool called;
  Status status;
  uint64_t generation;
  std::vector<CommitId> missing;
  storage_->GetGenerationAndMissingParents(
      id_and_bytes,
      callback::Capture(callback::SetWhenCalled(&called), &status, &generation, &missing));
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(generation, commit_to_add->GetGeneration());
  EXPECT_THAT(missing, ElementsAre(missing_parent->GetId()));
}

TEST_F(PageStorageTest, EagerLiveReferencesGarbageCollection) {
  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, data.object_identifier);

    // The object is available.
    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);

    // Release the references to the object.
    UntrackIdentifier(&object_identifier);
    piece.reset();

    // Give some time for the object to be collected.
    RunLoopUntilIdle();

    // Check that it has been collected.
    RetrackIdentifier(&object_identifier);
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
  });
}

TEST_F(PageStorageTestEagerRootNodesGC, EagerRootNodesGarbageCollection) {
  ResetStorage(CommitPruningPolicy::LOCAL_IMMEDIATE);

  RunInCoroutine([this](CoroutineHandler* handler) {
    ObjectData data = MakeObject("Some data", InlineBehavior::PREVENT);

    bool called;
    Status status;
    ObjectIdentifier object_identifier;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(object_identifier, data.object_identifier);

    // The object is available.
    std::unique_ptr<const Piece> piece;
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);

    // Create 3 commits: The first one contains the object we just created. The other two are
    // necessary so that the first one is pruned and its root identifier has no references.

    // Commit 1: Add the object in a commit.
    std::unique_ptr<Journal> journal = storage_->StartCommit(GetFirstHead());
    journal->Put("key", object_identifier, KeyPriority::EAGER);
    std::unique_ptr<const Commit> commit = TryCommitJournal(std::move(journal), Status::OK);
    EXPECT_TRUE(commit);
    ObjectIdentifier root_identifier = commit->GetRootIdentifier();

    // Both the value object and the root node are available.
    ASSERT_EQ(ReadObject(handler, object_identifier, &piece), Status::OK);
    ASSERT_EQ(ReadObject(handler, root_identifier, &piece), Status::OK);

    // Commit 2: Add another object.
    ObjectData more_data = MakeObject("Some more data", InlineBehavior::PREVENT);
    ObjectIdentifier object_identifier2;
    storage_->AddObjectFromLocal(
        ObjectType::BLOB, more_data.ToDataSource(), {},
        callback::Capture(callback::SetWhenCalled(&called), &status, &object_identifier2));
    RunLoopUntilIdle();
    journal = storage_->StartCommit(std::move(commit));
    journal->Put("key", object_identifier2, KeyPriority::EAGER);
    commit = TryCommitJournal(std::move(journal), Status::OK);
    EXPECT_TRUE(commit);

    // Commit 3: Remove all contents.
    journal = storage_->StartCommit(std::move(commit));
    journal->Clear();
    commit = TryCommitJournal(std::move(journal), Status::OK);
    EXPECT_TRUE(commit);

    // Release the references to the objects.
    UntrackIdentifier(&object_identifier);
    UntrackIdentifier(&root_identifier);
    piece.reset();

    // Give some time for the object to be collected.
    RunLoopUntilIdle();

    // Check that it has been collected.
    RetrackIdentifier(&object_identifier);
    EXPECT_EQ(ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
    RetrackIdentifier(&root_identifier);
    EXPECT_EQ(ReadObject(handler, root_identifier, &piece), Status::INTERNAL_NOT_FOUND);
  });
}

// Tests that the object identifiers of parents are not garbage collected between the time the
// tree is written to disk, and the time the commit is written to disk.
TEST_F(PageStorageTest, CommitJournalKeepsParents) {
  // We create the following tree, with commits numbered by creation order:
  //    (1)
  //   /  \
  // (2)  (3)
  // Between (2) and (3), we mark (1) and (2) and their root objects as synced: the root object of
  // (1) becomes garbage collectable as soon as we drop our reference to (1).
  // Create two commits, mark the first one as synced, as well as its root object.
  bool called;
  Status status;
  std::unique_ptr<const Commit> commit1 = TryCommitFromLocal(10);
  ASSERT_TRUE(commit1);
  std::unique_ptr<const Commit> commit2 = TryCommitFromLocal(10);
  ASSERT_TRUE(commit2);

  CommitId commit1_id = commit1->GetId();
  ObjectIdentifier commit1_root_identifier = commit1->GetRootIdentifier();
  MarkCommitSynced(commit1_id);
  MarkCommitSynced(commit2->GetId());
  MarkPieceSynced(commit1_root_identifier);
  UntrackIdentifier(&commit1_root_identifier);

  std::unique_ptr<Journal> journal = storage_->StartCommit(std::move(commit1));
  journal->Put("key", RandomObjectIdentifier(), KeyPriority::EAGER);
  // Take the commit lock before committing. This will leave time for garbage collection.
  fit::closure unlock;
  PageStorageImplAccessorForTest::GetCommitSerializer(storage_).Serialize(
      [] {}, callback::Capture(callback::SetWhenCalled(&called), &unlock));
  ASSERT_TRUE(called);
  std::unique_ptr<const Commit> commit3;
  storage_->CommitJournal(std::move(journal),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &commit3));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  unlock();
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_TRUE(commit3);

  // The root of |commit1| is alive, so the diff can be computed.
  called = false;
  storage_->GetDiffForCloud(*commit3, [&](Status status, CommitIdView base_commit_id,
                                          std::vector<EntryChange> /*changes*/) {
    called = true;
    EXPECT_EQ(base_commit_id, commit1_id);
    EXPECT_EQ(status, Status::OK);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_GT(PageStorageImplAccessorForTest::CountLiveReferences(
                storage_, commit1_root_identifier.object_digest()),
            0);

  // Mark commit3 as synced: the root of commit1 is not needed anymore.
  MarkCommitSynced(commit3->GetId());
  ASSERT_EQ(PageStorageImplAccessorForTest::CountLiveReferences(
                storage_, commit1_root_identifier.object_digest()),
            0);
}

}  // namespace

}  // namespace storage
