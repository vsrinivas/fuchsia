// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <vector>

#include "src/ledger/bin/clocks/public/device_id_manager.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/impl/commit_factory.h"
#include "src/ledger/bin/storage/impl/commit_pruner.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/page_db_impl.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/managed_container.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/lib/callback/operation_serializer.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

class PageStorageImpl : public PageStorage, public CommitPruner::CommitPrunerDelegate {
 public:
  PageStorageImpl(ledger::Environment* environment,
                  encryption::EncryptionService* encryption_service, std::unique_ptr<Db> db,
                  PageId page_id, CommitPruningPolicy policy);
  PageStorageImpl(ledger::Environment* environment,
                  encryption::EncryptionService* encryption_service,
                  std::unique_ptr<PageDb> page_db, PageId page_id, CommitPruningPolicy policy);

  PageStorageImpl(const PageStorageImpl&) = delete;
  PageStorageImpl& operator=(const PageStorageImpl&) = delete;
  ~PageStorageImpl() override;

  // Initializes this PageStorageImpl. This includes initializing the underlying
  // database and adding the default page head if the page is empty.
  void Init(clocks::DeviceIdManager* device_id_manager, fit::function<void(Status)> callback);

  // Checks whether the given |object_identifier| is untracked, i.e. has been
  // created using |AddObjectFromLocal()|, but is not yet part of any commit.
  // Untracked objects are invalid after the PageStorageImpl object is
  // destroyed.
  void ObjectIsUntracked(ObjectIdentifier object_identifier,
                         fit::function<void(Status, bool)> callback);

  std::string GetEntryId();

  std::string GetEntryIdForMerge(absl::string_view entry_name, CommitIdView left_parent_id,
                                 CommitIdView right_parent_id, absl::string_view operation_list);

  void SetSyncDelegate(PageSyncDelegate* page_sync) override;

  // PageStorage:
  PageId GetId() override;
  ObjectIdentifierFactory* GetObjectIdentifierFactory() override;
  Status GetHeadCommits(std::vector<std::unique_ptr<const Commit>>* head_commits) override;
  void GetMergeCommitIds(CommitIdView parent1_id, CommitIdView parent2_id,
                         fit::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetCommit(CommitIdView commit_id,
                 fit::function<void(Status, std::unique_ptr<const Commit>)> callback) override;
  void GetGenerationAndMissingParents(
      const CommitIdAndBytes& id_and_bytes,
      fit::function<void(Status, uint64_t, std::vector<CommitId>)> callback) override;
  void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
                          fit::function<void(Status)> callback) override;
  std::unique_ptr<Journal> StartCommit(std::unique_ptr<const Commit> commit_id) override;
  std::unique_ptr<Journal> StartMergeCommit(std::unique_ptr<const Commit> left,
                                            std::unique_ptr<const Commit> right) override;
  void CommitJournal(std::unique_ptr<Journal> journal,
                     fit::function<void(Status, std::unique_ptr<const Commit>)> callback) override;
  void AddCommitWatcher(CommitWatcher* watcher) override;
  void RemoveCommitWatcher(CommitWatcher* watcher) override;
  void IsSynced(fit::function<void(Status, bool)> callback) override;
  bool IsOnline() override;
  void IsEmpty(fit::function<void(Status, bool)> callback) override;
  void GetUnsyncedCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) override;
  void MarkCommitSynced(const CommitId& commit_id, fit::function<void(Status)> callback) override;
  void GetUnsyncedPieces(
      fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) override;
  void MarkPieceSynced(ObjectIdentifier object_identifier,
                       fit::function<void(Status)> callback) override;
  void IsPieceSynced(ObjectIdentifier object_identifier,
                     fit::function<void(Status, bool)> callback) override;
  void MarkSyncedToPeer(fit::function<void(Status)> callback) override;
  void AddObjectFromLocal(ObjectType object_type, std::unique_ptr<DataSource> data_source,
                          ObjectReferencesAndPriority tree_references,
                          fit::function<void(Status, ObjectIdentifier)> callback) override;
  void GetObjectPart(ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
                     Location location,
                     fit::function<void(Status, ledger::SizedVmo)> callback) override;
  void GetObject(ObjectIdentifier object_identifier, Location location,
                 fit::function<void(Status, std::unique_ptr<const Object>)> callback) override;
  void GetPiece(ObjectIdentifier object_identifier,
                fit::function<void(Status, std::unique_ptr<const Piece>)> callback) override;
  void SetSyncMetadata(absl::string_view key, absl::string_view value,
                       fit::function<void(Status)> callback) override;
  void GetSyncMetadata(absl::string_view key,
                       fit::function<void(Status, std::string)> callback) override;

  // Commit contents.
  void GetCommitContents(const Commit& commit, std::string min_key,
                         fit::function<bool(Entry)> on_next,
                         fit::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit, std::string key,
                          fit::function<void(Status, Entry)> callback) override;
  void GetDiffForCloud(
      const Commit& target_commit,
      fit::function<void(Status, CommitIdView, std::vector<EntryChange>)> callback) override;
  void GetCommitContentsDiff(const Commit& base_commit, const Commit& other_commit,
                             std::string min_key, fit::function<bool(EntryChange)> on_next_diff,
                             fit::function<void(Status)> on_done) override;
  void GetThreeWayContentsDiff(const Commit& base_commit, const Commit& left_commit,
                               const Commit& right_commit, std::string min_key,
                               fit::function<bool(ThreeWayChange)> on_next_diff,
                               fit::function<void(Status)> on_done) override;

  void GetClock(fit::function<void(Status, Clock)> callback) override;

  void GetCommitIdFromRemoteId(absl::string_view remote_commit_id,
                               fit::function<void(Status, CommitId)> callback) override;

  // CommitPrunerDelegate:
  Status DeleteCommits(coroutine::CoroutineHandler* handler,
                       std::vector<std::unique_ptr<const Commit>> commits) override;
  Status SetClock(coroutine::CoroutineHandler* handler, const Clock& clock) override;

  CommitFactory* GetCommitFactory();

 private:
  friend class PageStorageImplAccessorForTest;

  // A function that accepts a |piece|, an |object| and a |callback|. It
  // attempts to extract references from the |piece| and the |object| (which
  // must have the same object identifier) and to add the |piece| to storage
  // with those references. On success, returns |object| to the |callback|. On
  // failure, returns the error, and a nullptr as a second parameter (ie. drops
  // the |object|). See |GetOrDownloadPiece| for details on usage.
  using WritePieceCallback =
      fit::function<void(std::unique_ptr<const Piece>, std::unique_ptr<const Object>,
                         fit::function<void(Status, std::unique_ptr<const Object>)>)>;

  // Marks all pieces needed for the given objects as local.
  ABSL_MUST_USE_RESULT Status MarkAllPiecesLocal(coroutine::CoroutineHandler* handler,
                                                 PageDb::Batch* batch,
                                                 std::vector<ObjectIdentifier> object_identifiers);

  ABSL_MUST_USE_RESULT Status ContainsCommit(coroutine::CoroutineHandler* handler, CommitIdView id);

  bool IsFirstCommit(CommitIdView id);

  // Adds the given synced |piece| object.
  void AddPiece(std::unique_ptr<const Piece> piece, ChangeSource source,
                IsObjectSynced is_object_synced, ObjectReferencesAndPriority references,
                fit::function<void(Status)> callback);

  // Returns the piece identified by |object_identifier|. |location| is either LOCAL and NETWORK,
  // and defines whether the piece should be looked up remotely if not available locally.
  // When the piece has been retrieved remotely, attempts to add it to storage before returning
  // it. If this is not possible, ie. when the piece is an index tree-node that requires the full
  // object to compute its references, also returns a WritePieceCallback. It is the callers
  // responsability to invoke this callback to add the piece to storage once they have gathered
  // the full object. The WritePieceCallback is safe to call as long as this class is valid. It
  // should not outlive the returned piece (since a reference to the piece must be passed to it
  // when invoked), and in practice should be called as soon as the full object containing the
  // piece has been constructed to ensure data is persisted to disk as early as possible.
  void GetOrDownloadPiece(
      ObjectIdentifier object_identifier, Location location,
      fit::function<void(Status, std::unique_ptr<const Piece>, WritePieceCallback)> callback);

  // Same interface as |GetOrDownloadPiece|, but only fetches using |GetObject| and does not check
  // if the object is present locally.
  void DownloadPieceDirectly(
      ObjectIdentifier object_identifier, Location location,
      fit::function<void(Status, std::unique_ptr<const Piece>, WritePieceCallback)> callback);

  // Same interface as |GetOrDownloadPiece|, but only fetches by applying a diff and does not check
  // if the object is present locally. No |WritePieceCallback| is returned.
  void DownloadPieceUsingDiff(ObjectIdentifier object_identifier, CommitId containing_commit,
                              fit::function<void(Status, std::unique_ptr<const Piece>)> callback);

  // Reads the content of a piece into a provided VMO. Takes into account the global offset and size
  // in order to be able to read only the requested part of an object.
  // |global_offset| is the offset from the beginning of the full object in bytes. |global_size| is
  // the maximum size requested to be read into the vmo. |current_position| is the position of the
  // currently read piece (defined by |object_identifier|) in the full object. |object_size| is the
  // size of the currently read piece.
  // |location| is either LOCAL and NETWORK and defines the behavior in the case where the object is
  // not found locally.
  void FillBufferWithObjectContent(const Piece& piece, ledger::SizedVmo vmo, int64_t global_offset,
                                   int64_t global_size, int64_t current_position,
                                   int64_t object_size, Location location,
                                   fit::function<void(Status)> callback);

  // Treating the |piece| as FileIndex, initializes a VMO of a needed size and calls
  // FillBufferWithObjectContent on it.
  // |offset| and |max_size| are used to denote partial mapping (see GetObjectPart for details).
  // This method fills |child_identifiers|, if not nullptr, with the identifiers of the direct
  // children of |piece|.
  void GetIndexObject(const Piece& piece, int64_t offset, int64_t max_size, Location location,
                      std::vector<ObjectIdentifier>* child_identifiers,
                      fit::function<void(Status, ledger::SizedVmo)> callback);

  // Notifies the registered watchers of |new_commits|.
  void NotifyWatchersOfNewCommits(const std::vector<std::unique_ptr<const Commit>>& new_commits,
                                  ChangeSource source);

  // Find the root identifier of a commit, even if it is in the process of being added to the
  // storage. This breaks a circual dependency: |GetObject| needs the root identifier of commits to
  // apply and check diffs, but we need to get the objects referred to by a commit before adding it
  // in the storage.
  void GetCommitRootIdentifier(CommitIdView commit_id,
                               fit::function<void(Status, ObjectIdentifier)> callback);

  // Deletes the piece identifier by |object_digest| from local storage. On success, returns the
  // references from the deleted piece to other pieces. Aborts if there is already a pending
  // deletion of |object_digest|.
  Status DeleteObject(coroutine::CoroutineHandler* handler, ObjectDigest object_digest,
                      ObjectReferencesAndPriority* references);

  // Attempts to delete |object_digest|, and recursively schedules the deletion of the objects it
  // references upon success.
  void ScheduleObjectGarbageCollection(const ObjectDigest& object_digest);

  // Synchronous versions of API methods using coroutines.
  ABSL_MUST_USE_RESULT Status SynchronousInit(coroutine::CoroutineHandler* handler,
                                              clocks::DeviceIdManager* device_id_manager);

  ABSL_MUST_USE_RESULT Status SynchronousGetCommit(coroutine::CoroutineHandler* handler,
                                                   CommitId commit_id,
                                                   std::unique_ptr<const Commit>* commit);

  // Adds the given locally created |commit| in this |PageStorage|.
  ABSL_MUST_USE_RESULT Status SynchronousAddCommitFromLocal(
      coroutine::CoroutineHandler* handler, std::unique_ptr<const Commit> commit,
      std::vector<ObjectIdentifier> new_objects);

  ABSL_MUST_USE_RESULT Status
  SynchronousAddCommitsFromSync(coroutine::CoroutineHandler* handler,
                                std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source);

  ABSL_MUST_USE_RESULT Status
  SynchronousGetUnsyncedCommits(coroutine::CoroutineHandler* handler,
                                std::vector<std::unique_ptr<const Commit>>* unsynced_commits);

  ABSL_MUST_USE_RESULT Status SynchronousMarkCommitSynced(coroutine::CoroutineHandler* handler,
                                                          const Commit& commit);

  ABSL_MUST_USE_RESULT Status SynchronousMarkCommitSyncedInBatch(
      coroutine::CoroutineHandler* handler, PageDb::Batch* batch, const CommitId& commit_id);

  ABSL_MUST_USE_RESULT Status SynchronousAddCommits(
      coroutine::CoroutineHandler* handler, std::vector<std::unique_ptr<const Commit>> commits,
      ChangeSource source, std::vector<ObjectIdentifier> new_objects);

  ABSL_MUST_USE_RESULT Status SynchronousAddPiece(coroutine::CoroutineHandler* handler,
                                                  const Piece& piece, ChangeSource source,
                                                  IsObjectSynced is_object_synced,
                                                  ObjectReferencesAndPriority references);

  ABSL_MUST_USE_RESULT Status SynchronousDownloadDiff(coroutine::CoroutineHandler* handler,
                                                      CommitId target_commit_id);

  // Synchronous helper methods.

  // Marks this page as online.
  ABSL_MUST_USE_RESULT Status SynchronousMarkPageOnline(coroutine::CoroutineHandler* handler,
                                                        PageDb::Batch* batch);

  // Updates the given |empty_node_id| to point to the empty node's
  // ObjectIdentifier.
  ABSL_MUST_USE_RESULT Status SynchronousGetEmptyNodeIdentifier(
      coroutine::CoroutineHandler* handler, ObjectIdentifier** empty_node_id);

  // Returns the root identifier of the base parent of |commit|.
  ABSL_MUST_USE_RESULT Status
  GetBaseParentRootIdentifier(coroutine::CoroutineHandler* handler, const Commit& commit,
                              ObjectIdentifier* base_parent_root_identifier);

  // Checks if a tracked object identifier is tracked by this PageStorage.
  // Returns true for all untracked object identifiers.
  bool IsTokenValid(const ObjectIdentifier& object_identifier);

  // Finds good commits to use as diff bases for |target_id|. The commits will be locally present
  // and synced.
  void ChooseDiffBases(CommitIdView target_id,
                       fit::callback<void(Status, std::vector<CommitId>)> callback);

  ledger::Environment* environment_;
  encryption::EncryptionService* const encryption_service_;
  const PageId page_id_;
  ObjectIdentifierFactoryImpl object_identifier_factory_;
  // Objects currently handled by |DeleteObject|.
  std::set<ObjectDigest> pending_garbage_collection_;
  CommitFactory commit_factory_;
  std::unique_ptr<PageDb> db_;
  // The commit pruner accesses the database, it must be destructed before |db_|.
  CommitPruner commit_pruner_;
  fxl::ObserverList<CommitWatcher> watchers_;
  ledger::ManagedContainer managed_container_;
  PageSyncDelegate* page_sync_;
  bool page_is_online_ = false;
  std::unique_ptr<ObjectIdentifier> empty_node_id_ = nullptr;
  // Temporarily stores the root of commits being added from sync, so they can be used to apply
  // diffs. A commit will be removed from this set once it is successfully added to the storage.
  std::map<CommitId, ObjectIdentifier, std::less<>> roots_of_commits_being_added_;
  // Temporarily stores the mapping from remote commit id to local commit id for commits that have
  // not yet been added to the storage. A commit will be removed from this set once it is
  // successfully added to the storage.
  std::map<std::string, CommitId, std::less<>> remote_ids_of_commits_being_added_;
  // Identifier for this device on the page clock. It does not need to be consistent across pages.
  clocks::DeviceId device_id_;

  callback::OperationSerializer commit_serializer_;
  coroutine::CoroutineManager download_manager_;
  coroutine::CoroutineManager coroutine_manager_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<PageStorageImpl> weak_factory_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
