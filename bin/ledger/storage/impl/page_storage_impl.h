// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include <lib/async/dispatcher.h>

#include <queue>
#include <set>
#include <vector>

#include "lib/callback/managed_container.h"
#include "lib/callback/operation_serializer.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/storage/impl/page_db_impl.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/page_sync_delegate.h"
#include "peridot/lib/convert/convert.h"

namespace storage {

class PageStorageImpl : public PageStorage {
 public:
  PageStorageImpl(async_t* async,
                  coroutine::CoroutineService* coroutine_service,
                  encryption::EncryptionService* encryption_service,
                  ledger::DetachedPath page_dir, PageId page_id);
  PageStorageImpl(async_t* async,
                  coroutine::CoroutineService* coroutine_service,
                  encryption::EncryptionService* encryption_service,
                  std::unique_ptr<PageDb> page_db, PageId page_id);

  ~PageStorageImpl() override;

  // Initializes this PageStorageImpl. This includes initializing the underlying
  // database, adding the default page head if the page is empty, removing
  // uncommitted explicit and committing implicit journals.
  void Init(std::function<void(Status)> callback);

  // Adds the given locally created |commit| in this |PageStorage|.
  void AddCommitFromLocal(std::unique_ptr<const Commit> commit,
                          std::vector<ObjectIdentifier> new_objects,
                          std::function<void(Status)> callback);

  // Checks whether the given |object_identifier| is untracked, i.e. has been
  // created using |AddObjectFromLocal()|, but is not yet part of any commit.
  // Untracked objects are invalid after the PageStorageImpl object is
  // destroyed.
  void ObjectIsUntracked(ObjectIdentifier object_identifier,
                         std::function<void(Status, bool)> callback);

  // PageStorage:
  PageId GetId() override;
  void SetSyncDelegate(PageSyncDelegate* page_sync) override;
  void GetHeadCommitIds(
      std::function<void(Status, std::vector<CommitId>)> callback) override;
  void GetCommit(CommitIdView commit_id,
                 std::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;
  void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                          ChangeSource source,
                          std::function<void(Status)> callback) override;
  void StartCommit(
      const CommitId& commit_id, JournalType journal_type,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;
  void StartMergeCommit(
      const CommitId& left, const CommitId& right,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;
  void CommitJournal(std::unique_ptr<Journal> journal,
                     std::function<void(Status, std::unique_ptr<const Commit>)>
                         callback) override;
  void RollbackJournal(std::unique_ptr<Journal> journal,
                       std::function<void(Status)> callback) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  void IsSynced(std::function<void(Status, bool)> callback) override;
  void GetUnsyncedCommits(
      std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) override;
  void MarkCommitSynced(const CommitId& commit_id,
                        std::function<void(Status)> callback) override;
  void GetUnsyncedPieces(
      std::function<void(Status, std::vector<ObjectIdentifier>)> callback)
      override;
  void MarkPieceSynced(ObjectIdentifier object_identifier,
                       std::function<void(Status)> callback) override;
  void IsPieceSynced(ObjectIdentifier object_identifier,
                     std::function<void(Status, bool)> callback) override;
  void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      std::function<void(Status, ObjectIdentifier)> callback) override;
  void GetObject(ObjectIdentifier object_identifier, Location location,
                 std::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;
  void GetPiece(ObjectIdentifier object_identifier,
                std::function<void(Status, std::unique_ptr<const Object>)>
                    callback) override;
  void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                       std::function<void(Status)> callback) override;
  void GetSyncMetadata(
      fxl::StringView key,
      std::function<void(Status, std::string)> callback) override;

  // Methods to be used by JournalImpl.
  void GetJournalEntries(
      const JournalId& journal_id,
      std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
          callback);

  void AddJournalEntry(const JournalId& journal_id, fxl::StringView key,
                       ObjectIdentifier object_identifier, KeyPriority priority,
                       std::function<void(Status)> callback);

  void RemoveJournalEntry(const JournalId& journal_id,
                          convert::ExtendedStringView key,
                          std::function<void(Status)> callback);

  void RemoveJournal(const JournalId& journal_id,
                     std::function<void(Status)> callback);

  // Commit contents.
  void GetCommitContents(const Commit& commit, std::string min_key,
                         std::function<bool(Entry)> on_next,
                         std::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit, std::string key,
                          std::function<void(Status, Entry)> callback) override;
  void GetCommitContentsDiff(const Commit& base_commit,
                             const Commit& other_commit, std::string min_key,
                             std::function<bool(EntryChange)> on_next_diff,
                             std::function<void(Status)> on_done) override;
  void GetThreeWayContentsDiff(const Commit& base_commit,
                               const Commit& left_commit,
                               const Commit& right_commit, std::string min_key,
                               std::function<bool(ThreeWayChange)> on_next_diff,
                               std::function<void(Status)> on_done) override;

 private:
  friend class PageStorageImplAccessorForTest;

  // Marks all pieces needed for the given objects as local.
  FXL_WARN_UNUSED_RESULT Status
  MarkAllPiecesLocal(coroutine::CoroutineHandler* handler, PageDb::Batch* batch,
                     std::vector<ObjectIdentifier> object_identifiers);

  FXL_WARN_UNUSED_RESULT Status
  ContainsCommit(coroutine::CoroutineHandler* handler, CommitIdView id);

  bool IsFirstCommit(CommitIdView id);

  // Adds the given synced object. |object_identifier| will be validated against
  // the expected one based on the |data| and an |OBJECT_DIGEST_MISSMATCH| error
  // will be returned in case of missmatch.
  void AddPiece(ObjectIdentifier object_identifier,
                std::unique_ptr<DataSource::DataChunk> data,
                ChangeSource source, std::function<void(Status)> callback);

  // Download all the chunks of the object with the given id.
  void DownloadFullObject(ObjectIdentifier object_identifier,
                          std::function<void(Status)> callback);

  void GetObjectFromSync(
      ObjectIdentifier object_identifier,
      std::function<void(Status, std::unique_ptr<const Object>)> callback);

  void FillBufferWithObjectContent(ObjectIdentifier object_identifier,
                                   fsl::SizedVmo vmo, size_t offset,
                                   size_t size,
                                   std::function<void(Status)> callback);

  // Notifies the registered watchers with the |commits| in commit_to_send_.
  void NotifyWatchers();

  // Immediately adds the |handler| in the set of active ones, and once the
  // returned callback is called, removes the |handler| from the set, and calls
  // the given |callback|.
  template <typename... Args>
  std::function<void(Args...)> UpdateActiveHandlersCallback(
      coroutine::CoroutineHandler* handler,
      std::function<void(Args...)> callback) {
    handlers_.insert(handler);
    return [this, handler, callback = std::move(callback)](Args... args) {
      // Remove the handler before calling the final callback. Otherwise the
      // handler might be unnecessarily interrupted, if this PageStorage
      // destructor is called in the callback.
      handlers_.erase(handler);
      callback(std::forward<Args>(args)...);
    };
  }

  // Synchronous versions of API methods using coroutines.
  FXL_WARN_UNUSED_RESULT Status
  SynchronousInit(coroutine::CoroutineHandler* handler);

  FXL_WARN_UNUSED_RESULT Status
  SynchronousGetCommit(coroutine::CoroutineHandler* handler, CommitId commit_id,
                       std::unique_ptr<const Commit>* commit);

  FXL_WARN_UNUSED_RESULT Status
  SynchronousAddCommitFromLocal(coroutine::CoroutineHandler* handler,
                                std::unique_ptr<const Commit> commit,
                                std::vector<ObjectIdentifier> new_objects);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddCommitsFromSync(
      coroutine::CoroutineHandler* handler,
      std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source);

  FXL_WARN_UNUSED_RESULT Status SynchronousGetUnsyncedCommits(
      coroutine::CoroutineHandler* handler,
      std::vector<std::unique_ptr<const Commit>>* unsynced_commits);

  FXL_WARN_UNUSED_RESULT Status SynchronousMarkCommitSynced(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddCommits(
      coroutine::CoroutineHandler* handler,
      std::vector<std::unique_ptr<const Commit>> commits, ChangeSource source,
      std::vector<ObjectIdentifier> new_objects);

  FXL_WARN_UNUSED_RESULT Status SynchronousAddPiece(
      coroutine::CoroutineHandler* handler, ObjectIdentifier object_identifier,
      std::unique_ptr<DataSource::DataChunk> data, ChangeSource source);

  async_t* const async_;
  coroutine::CoroutineService* const coroutine_service_;
  encryption::EncryptionService* const encryption_service_;
  const PageId page_id_;
  std::unique_ptr<PageDb> db_;
  std::vector<CommitWatcher*> watchers_;
  callback::ManagedContainer managed_container_;
  PageSyncDelegate* page_sync_;
  std::queue<
      std::pair<ChangeSource, std::vector<std::unique_ptr<const Commit>>>>
      commits_to_send_;
  // The set of active handlers.
  std::set<coroutine::CoroutineHandler*> handlers_;

  callback::OperationSerializer commit_serializer_;

#ifndef NDEBUG
  // Only one commit insertion should be in progress at a time in storage.
  // |commit_in_progress_| keeps track of whether such an insertion is in
  // progress.
  bool commit_in_progress_ = false;
#endif
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
