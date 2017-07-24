// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_storage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

#include "apps/ledger/src/callback/asynchronous_callback.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/cobalt/cobalt.h"
#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/btree/diff.h"
#include "apps/ledger/src/storage/impl/btree/iterator.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/constants.h"
#include "apps/ledger/src/storage/impl/file_index.h"
#include "apps/ledger/src/storage/impl/file_index_generated.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/impl/object_id.h"
#include "apps/ledger/src/storage/impl/object_impl.h"
#include "apps/ledger/src/storage/impl/split.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/strings/concatenate.h"
#include "mx/vmar.h"
#include "mx/vmo.h"

namespace storage {

namespace {

using StreamingHash = glue::SHA256StreamingHash;

const char kLevelDbDir[] = "/leveldb";

static_assert(kStorageHashSize == StreamingHash::kHashSize,
              "Unexpected kStorageHashSize value");

struct StringPointerComparator {
  using is_transparent = std::true_type;

  bool operator()(const std::string* str1, const std::string* str2) const {
    return *str1 < *str2;
  }

  bool operator()(const std::string* str1, const CommitIdView* str2) const {
    return *str1 < *str2;
  }

  bool operator()(const CommitIdView* str1, const std::string* str2) const {
    return *str1 < *str2;
  }
};

Status RollbackJournalInternal(std::unique_ptr<Journal> journal) {
  return static_cast<JournalDBImpl*>(journal.get())->Rollback();
}

}  // namespace

PageStorageImpl::PageStorageImpl(coroutine::CoroutineService* coroutine_service,
                                 std::string page_dir,
                                 PageId page_id)
    : coroutine_service_(coroutine_service),
      page_id_(std::move(page_id)),
      db_(coroutine_service, this, page_dir + kLevelDbDir),
      page_sync_(nullptr) {}

PageStorageImpl::~PageStorageImpl() {}

void PageStorageImpl::Init(std::function<void(Status)> callback) {
  // Initialize PageDb.
  Status s = db_.Init();
  if (s != Status::OK) {
    callback(s);
    return;
  }

  // Add the default page head if this page is empty.
  std::vector<CommitId> heads;
  s = db_.GetHeads(&heads);
  if (s != Status::OK) {
    callback(s);
    return;
  }
  if (heads.empty()) {
    s = db_.AddHead(kFirstPageCommitId, 0);
    if (s != Status::OK) {
      callback(s);
      return;
    }
  }

  // Remove uncommited explicit journals.
  db_.RemoveExplicitJournals();

  // Commit uncommited implicit journals.
  std::vector<JournalId> journal_ids;
  s = db_.GetImplicitJournalIds(&journal_ids);
  if (s != Status::OK) {
    callback(s);
    return;
  }
  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  for (JournalId& id : journal_ids) {
    std::unique_ptr<Journal> journal;
    s = db_.GetImplicitJournal(id, &journal);
    if (s != Status::OK) {
      FTL_LOG(ERROR) << "Failed to get implicit journal with status " << s
                     << ". journal id: " << id;
      callback(s);
      return;
    }

    CommitJournal(
        std::move(journal), [status_callback = waiter->NewCallback()](
                                Status status, std::unique_ptr<const Commit>) {
          if (status != Status::OK) {
            FTL_LOG(ERROR) << "Failed to commit implicit journal created in "
                              "previous Ledger execution.";
          }
          status_callback(status);
        });
  }

  waiter->Finalize(std::move(callback));
}

PageId PageStorageImpl::GetId() {
  return page_id_;
}

void PageStorageImpl::SetSyncDelegate(PageSyncDelegate* page_sync) {
  page_sync_ = page_sync;
}

void PageStorageImpl::GetHeadCommitIds(
    std::function<void(Status, std::vector<CommitId>)> callback) {
  std::vector<CommitId> commit_ids;
  Status status = db_.GetHeads(&commit_ids);
  if (status != Status::OK) {
    callback(status, std::vector<CommitId>());
    return;
  }
  callback(Status::OK, std::move(commit_ids));
}

void PageStorageImpl::GetCommit(
    CommitIdView commit_id,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  if (IsFirstCommit(commit_id)) {
    CommitImpl::Empty(this, std::move(callback));
    return;
  }
  std::string bytes;
  Status s = db_.GetCommitStorageBytes(commit_id, &bytes);
  if (s != Status::OK) {
    callback(s, nullptr);
    return;
  }
  std::unique_ptr<const Commit> commit = CommitImpl::FromStorageBytes(
      this, commit_id.ToString(), std::move(bytes));
  if (!commit) {
    callback(Status::FORMAT_ERROR, nullptr);
    return;
  }
  callback(Status::OK, std::move(commit));
}

void PageStorageImpl::AddCommitFromLocal(std::unique_ptr<const Commit> commit,
                                         std::vector<ObjectId> new_objects,
                                         std::function<void(Status)> callback) {
  // If the commit is already present, do nothing.
  if (ContainsCommit(commit->GetId()) == Status::OK) {
    callback(Status::OK);
    return;
  }
  std::vector<std::unique_ptr<const Commit>> commits;
  commits.reserve(1);
  commits.push_back(std::move(commit));
  AddCommits(std::move(commits), ChangeSource::LOCAL, std::move(new_objects),
             callback);
}

void PageStorageImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> ids_and_bytes,
    std::function<void(Status)> callback) {
  std::vector<std::unique_ptr<const Commit>> commits;

  std::map<const CommitId*, const Commit*, StringPointerComparator> leaves;
  commits.reserve(ids_and_bytes.size());

  for (auto& id_and_bytes : ids_and_bytes) {
    ObjectId id = std::move(id_and_bytes.id);
    std::string storage_bytes = std::move(id_and_bytes.bytes);
    if (ContainsCommit(id) == Status::OK) {
      MarkCommitSynced(id);
      continue;
    }

    std::unique_ptr<const Commit> commit =
        CommitImpl::FromStorageBytes(this, id, std::move(storage_bytes));
    if (!commit) {
      FTL_LOG(ERROR) << "Unable to add commit. Id: " << convert::ToHex(id);
      callback(Status::FORMAT_ERROR);
      return;
    }

    // Remove parents from leaves.
    for (const auto& parent_id : commit->GetParentIds()) {
      auto it = leaves.find(&parent_id);
      if (it != leaves.end()) {
        leaves.erase(it);
      }
    }
    leaves[&commit->GetId()] = commit.get();
    commits.push_back(std::move(commit));
  }

  if (commits.empty()) {
    callback(Status::OK);
    return;
  }

  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  // Get all objects from sync and then add the commit objects.
  for (const auto& leaf : leaves) {
    btree::GetObjectsFromSync(coroutine_service_, this,
                              leaf.second->GetRootId(), waiter->NewCallback());
  }

  waiter->Finalize(ftl::MakeCopyable([
    this, commits = std::move(commits), callback = std::move(callback)
  ](Status status) mutable {
    if (status != Status::OK) {
      callback(status);
      return;
    }

    AddCommits(std::move(commits), ChangeSource::SYNC, std::vector<ObjectId>(),
               callback);
  }));
}

Status PageStorageImpl::StartCommit(const CommitId& commit_id,
                                    JournalType journal_type,
                                    std::unique_ptr<Journal>* journal) {
  return db_.CreateJournal(journal_type, commit_id, journal);
}

Status PageStorageImpl::StartMergeCommit(const CommitId& left,
                                         const CommitId& right,
                                         std::unique_ptr<Journal>* journal) {
  return db_.CreateMergeJournal(left, right, journal);
}

void PageStorageImpl::CommitJournal(
    std::unique_ptr<Journal> journal,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  JournalDBImpl* journal_ptr = static_cast<JournalDBImpl*>(journal.get());
  // |journal| will now be owned by the Commit callback, making sure that it is
  // not deleted before the end of the computation.
  journal_ptr->Commit(ftl::MakeCopyable([
    journal = std::move(journal), callback = std::move(callback)
  ](Status status, std::unique_ptr<const storage::Commit> commit) mutable {
    if (status != Status::OK) {
      // Commit failed, roll the journal back.
      RollbackJournalInternal(std::move(journal));
    }
    callback(status, std::move(commit));
  }));
}

Status PageStorageImpl::RollbackJournal(std::unique_ptr<Journal> journal) {
  return RollbackJournalInternal(std::move(journal));
}

Status PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) {
  watchers_.push_back(watcher);
  return Status::OK;
}

Status PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  auto watcher_it =
      std::find_if(watchers_.begin(), watchers_.end(),
                   [watcher](CommitWatcher* w) { return w == watcher; });
  if (watcher_it == watchers_.end()) {
    return Status::NOT_FOUND;
  }
  watchers_.erase(watcher_it);
  return Status::OK;
}

void PageStorageImpl::GetUnsyncedCommits(
    std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
        callback) {
  std::vector<CommitId> commit_ids;
  Status s = db_.GetUnsyncedCommitIds(&commit_ids);
  if (s != Status::OK) {
    callback(s, {});
    return;
  }

  auto waiter = callback::Waiter<Status, std::unique_ptr<const Commit>>::Create(
      Status::OK);
  for (size_t i = 0; i < commit_ids.size(); ++i) {
    GetCommit(commit_ids[i], waiter->NewCallback());
  }
  waiter->Finalize([callback = std::move(callback)](
      Status s, std::vector<std::unique_ptr<const Commit>> commits) {
    if (s != Status::OK) {
      callback(s, {});
      return;
    }
    callback(Status::OK, std::move(commits));
  });
}

Status PageStorageImpl::MarkCommitSynced(const CommitId& commit_id) {
  return db_.MarkCommitIdSynced(commit_id);
}

Status PageStorageImpl::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<ObjectId>* objects) {
  return Status::NOT_IMPLEMENTED;
}

void PageStorageImpl::GetUnsyncedPieces(
    std::function<void(Status, std::vector<ObjectId>)> callback) {
  std::vector<ObjectId> unsynced_object_ids;
  Status s = db_.GetUnsyncedPieces(&unsynced_object_ids);
  callback(s, unsynced_object_ids);
}

Status PageStorageImpl::MarkPieceSynced(ObjectIdView object_id) {
  return db_.SetObjectStatus(object_id, PageDb::ObjectStatus::SYNCED);
}

void PageStorageImpl::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    const std::function<void(Status, ObjectId)>& callback) {
  auto traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_add_object");

  auto handler = pending_operation_manager_.Manage(std::move(data_source));
  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  SplitDataSource(
      handler.first->get(),
      ftl::MakeCopyable([
        this, waiter, cleanup = std::move(handler.second),
        callback = std::move(traced_callback)
      ](IterationStatus status, ObjectId object_id,
        std::unique_ptr<DataSource::DataChunk> chunk) mutable {
        if (status == IterationStatus::ERROR) {
          callback(Status::IO_ERROR, "");
          return;
        }
        if (chunk) {
          FTL_DCHECK(status == IterationStatus::IN_PROGRESS);

          if (GetObjectIdType(object_id) != ObjectIdType::INLINE) {
            AddPiece(std::move(object_id), std::move(chunk),
                     ChangeSource::LOCAL, waiter->NewCallback());
          }
          return;
        }

        FTL_DCHECK(status == IterationStatus::DONE);
        waiter->Finalize([
          object_id = std::move(object_id), callback = std::move(callback)
        ](Status status) mutable { callback(status, std::move(object_id)); });
      }));
}

void PageStorageImpl::GetObject(
    ObjectIdView object_id,
    Location location,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  GetPiece(object_id, [
    this, object_id = object_id.ToString(), location,
    callback = std::move(callback)
  ](Status status, std::unique_ptr<const Object> object) {
    if (status == Status::NOT_FOUND) {
      if (location == Location::NETWORK) {
        GetObjectFromSync(object_id, std::move(callback));
      } else {
        callback(Status::NOT_FOUND, nullptr);
      }
      return;
    }

    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    FTL_DCHECK(object);
    ObjectIdType id_type = GetObjectIdType(object_id);

    if (id_type == ObjectIdType::INLINE ||
        id_type == ObjectIdType::VALUE_HASH) {
      callback(status, std::move(object));
      return;
    }

    FTL_DCHECK(id_type == ObjectIdType::INDEX_HASH);

    ftl::StringView content;
    status = object->GetData(&content);
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    const FileIndex* file_index;
    status = FileIndexSerialization::ParseFileIndex(content, &file_index);
    if (status != Status::OK) {
      callback(Status::FORMAT_ERROR, nullptr);
      return;
    }

    mx::vmo vmo;
    mx_status_t mx_status = mx::vmo::create(file_index->size(), 0, &vmo);
    if (mx_status != MX_OK) {
      callback(Status::INTERNAL_IO_ERROR, nullptr);
      return;
    }

    size_t offset = 0;
    auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
    for (const auto* child : *file_index->children()) {
      if (offset + child->size() > file_index->size()) {
        callback(Status::FORMAT_ERROR, nullptr);
        return;
      }
      mx::vmo vmo_copy;
      mx_status_t mx_status =
          vmo.duplicate(MX_RIGHT_DUPLICATE | MX_RIGHT_WRITE, &vmo_copy);
      if (mx_status != MX_OK) {
        FTL_LOG(ERROR) << "Unable to duplicate vmo. Status: " << mx_status;
        callback(Status::INTERNAL_IO_ERROR, nullptr);
        return;
      }
      FillBufferWithObjectContent(child->object_id(), std::move(vmo_copy),
                                  offset, child->size(), waiter->NewCallback());
      offset += child->size();
    }
    if (offset != file_index->size()) {
      FTL_LOG(ERROR) << "Built file size doesn't add up.";
      callback(Status::FORMAT_ERROR, nullptr);
      return;
    }

    auto final_object =
        std::make_unique<VmoObject>(std::move(object_id), std::move(vmo));

    waiter->Finalize(ftl::MakeCopyable([
      object = std::move(final_object), callback = std::move(callback)
    ](Status status) mutable { callback(status, std::move(object)); }));
  });
}

void PageStorageImpl::GetPiece(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  ObjectIdType id_type = GetObjectIdType(object_id);
  if (id_type == ObjectIdType::INLINE) {
    callback(Status::OK, std::make_unique<InlinedObject>(object_id.ToString()));
    return;
  }

  std::unique_ptr<const Object> object;
  Status status = db_.ReadObject(object_id.ToString(), &object);
  callback(status, std::move(object));
}

Status PageStorageImpl::SetSyncMetadata(ftl::StringView key,
                                        ftl::StringView value) {
  return db_.SetSyncMetadata(key, value);
}

Status PageStorageImpl::GetSyncMetadata(ftl::StringView key,
                                        std::string* value) {
  return db_.GetSyncMetadata(key, value);
}

void PageStorageImpl::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        std::function<bool(Entry)> on_next,
                                        std::function<void(Status)> on_done) {
  btree::ForEachEntry(
      coroutine_service_, this, commit.GetRootId(), min_key,
      [on_next = std::move(on_next)](btree::EntryAndNodeId next) {
        return on_next(next.entry);
      },
      std::move(on_done));
}

void PageStorageImpl::GetEntryFromCommit(
    const Commit& commit,
    std::string key,
    std::function<void(Status, Entry)> callback) {
  std::unique_ptr<bool> key_found = std::make_unique<bool>(false);
  auto on_next = [ key, key_found = key_found.get(),
                   callback ](btree::EntryAndNodeId next) {
    if (next.entry.key == key) {
      *key_found = true;
      callback(Status::OK, next.entry);
    }
    return false;
  };

  auto on_done = ftl::MakeCopyable([
    key_found = std::move(key_found), callback = std::move(callback)
  ](Status s) mutable {
    if (*key_found) {
      return;
    }
    if (s == Status::OK) {
      callback(Status::NOT_FOUND, Entry());
      return;
    }
    callback(s, Entry());
  });
  btree::ForEachEntry(coroutine_service_, this, commit.GetRootId(),
                      std::move(key), std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetCommitContentsDiff(
    const Commit& base_commit,
    const Commit& other_commit,
    std::string min_key,
    std::function<bool(EntryChange)> on_next_diff,
    std::function<void(Status)> on_done) {
  btree::ForEachDiff(coroutine_service_, this, base_commit.GetRootId(),
                     other_commit.GetRootId(), std::move(min_key),
                     std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::NotifyWatchers() {
  while (!commits_to_send_.empty()) {
    auto to_send = std::move(commits_to_send_.front());
    for (CommitWatcher* watcher : watchers_) {
      watcher->OnNewCommits(std::move(to_send.second), to_send.first);
    }
    commits_to_send_.pop();
  }
}

Status PageStorageImpl::MarkAllPiecesLocal(std::vector<ObjectId> object_ids) {
  std::unordered_set<ObjectId> seen_ids;
  while (!object_ids.empty()) {
    auto it = seen_ids.insert(std::move(object_ids.back()));
    object_ids.pop_back();
    const ObjectId& object_id = *(it.first);
    FTL_DCHECK(GetObjectIdType(object_id) != ObjectIdType::INLINE);
    db_.SetObjectStatus(object_id, PageDb::ObjectStatus::LOCAL);
    if (GetObjectIdType(object_id) == ObjectIdType::INDEX_HASH) {
      std::unique_ptr<const Object> object;
      Status status = db_.ReadObject(object_id, &object);
      if (status != Status::OK) {
        return status;
      }

      ftl::StringView content;
      status = object->GetData(&content);
      if (status != Status::OK) {
        return status;
      }

      const FileIndex* file_index;
      status = FileIndexSerialization::ParseFileIndex(content, &file_index);
      if (status != Status::OK) {
        return status;
      }

      object_ids.reserve(object_ids.size() + file_index->children()->size());
      for (const auto* child : *file_index->children()) {
        if (GetObjectIdType(child->object_id()) != ObjectIdType::INLINE) {
          std::string new_object_id = convert::ToString(child->object_id());
          if (!seen_ids.count(new_object_id)) {
            object_ids.push_back(std::move(new_object_id));
          }
        }
      }
    }
  }
  return Status::OK;
}

void PageStorageImpl::AddCommits(
    std::vector<std::unique_ptr<const Commit>> commits,
    ChangeSource source,
    std::vector<ObjectId> new_objects,
    std::function<void(Status)> callback) {
  FTL_DCHECK(new_objects.empty() || source == ChangeSource::LOCAL)
      << "New objects must only be used when adding local commit.";

  // Apply all changes atomically.
  std::unique_ptr<PageDb::Batch> batch = db_.StartBatch();
  std::set<const CommitId*, StringPointerComparator> added_commits;
  std::vector<std::unique_ptr<const Commit>> commits_to_send;

  // If commits arrive out of order, some commits might be skipped. Continue
  // trying adding commits as long as at least one commit is added on each
  // iteration.
  bool continue_trying = true;
  while (continue_trying && !commits.empty()) {
    continue_trying = false;
    std::vector<std::unique_ptr<const Commit>> remaining_commits;

    for (auto& commit : commits) {
      Status s;

      // Commits should arrive in order. Check that the parents are either
      // present in PageDb or in the list of already processed commits.
      // If the commit arrive out of order, print an error, but skip it
      // temporarly so that the Ledger can recover if all the needed commits are
      // received in a single batch.
      for (const CommitIdView& parent_id : commit->GetParentIds()) {
        if (added_commits.count(&parent_id) == 0) {
          s = ContainsCommit(parent_id);
          if (s != Status::OK) {
            FTL_LOG(ERROR) << "Failed to find parent commit \""
                           << ToHex(parent_id) << "\" of commit \""
                           << convert::ToHex(commit->GetId())
                           << "\". Temporarily skipping in case the commits "
                              "are out of order.";
            if (s == Status::NOT_FOUND) {
              remaining_commits.push_back(std::move(commit));
              commit.reset();
              break;
            } else {
              callback(Status::INTERNAL_IO_ERROR);
            }
            return;
          }
        }
        // Remove the parent from the list of heads.
        db_.RemoveHead(parent_id);
      }

      // The commit could not be added. Skip it.
      if (!commit) {
        continue;
      }

      continue_trying = true;

      s = db_.AddCommitStorageBytes(commit->GetId(), commit->GetStorageBytes());
      if (s != Status::OK) {
        callback(s);
        return;
      }

      if (source == ChangeSource::LOCAL) {
        s = db_.MarkCommitIdUnsynced(commit->GetId(), commit->GetTimestamp());
        if (s != Status::OK) {
          callback(s);
          return;
        }
      }

      // Update heads.
      s = db_.AddHead(commit->GetId(), commit->GetTimestamp());
      if (s != Status::OK) {
        callback(s);
        return;
      }

      added_commits.insert(&commit->GetId());
      commits_to_send.push_back(std::move(commit));
    }

    if (!remaining_commits.empty()) {
      // If |remaining_commits| is not empty, some commits were our of order.
      ledger::ReportEvent(ledger::CobaltEvent::COMMITS_RECEIVED_OUT_OF_ORDER);
    }
    std::swap(commits, remaining_commits);
  }

  if (!commits.empty()) {
    FTL_LOG(ERROR) << "Failed adding commits. Found " << commits.size()
                   << " orphaned commits.";
    callback(Status::ILLEGAL_STATE);
    return;
  }

  // If adding local commits, mark all new pieces as local.
  Status status = MarkAllPiecesLocal(std::move(new_objects));
  if (status != Status::OK) {
    callback(status);
    return;
  }

  status = batch->Execute();
  bool notify_watchers = commits_to_send_.empty();
  commits_to_send_.emplace(source, std::move(commits_to_send));
  callback(status);

  if (status == Status::OK && notify_watchers) {
    NotifyWatchers();
  }
}

Status PageStorageImpl::ContainsCommit(CommitIdView id) {
  if (IsFirstCommit(id)) {
    return Status::OK;
  }
  std::string bytes;
  return db_.GetCommitStorageBytes(id, &bytes);
}

bool PageStorageImpl::IsFirstCommit(CommitIdView id) {
  return id == kFirstPageCommitId;
}

void PageStorageImpl::AddPiece(ObjectId object_id,
                               std::unique_ptr<DataSource::DataChunk> data,
                               ChangeSource source,
                               std::function<void(Status)> callback) {
  FTL_DCHECK(GetObjectIdType(object_id) != ObjectIdType::INLINE);
  FTL_DCHECK(
      object_id ==
      ComputeObjectId(GetObjectType(GetObjectIdType(object_id)), data->Get()));

  std::unique_ptr<const Object> object;
  Status status = db_.ReadObject(object_id, &object);
  if (status == Status::NOT_FOUND) {
    PageDb::ObjectStatus object_status =
        (source == ChangeSource::LOCAL ? PageDb::ObjectStatus::TRANSIENT
                                       : PageDb::ObjectStatus::SYNCED);
    callback(db_.WriteObject(object_id, std::move(data), object_status));
    return;
  }
  callback(status);
}

void PageStorageImpl::DownloadFullObject(ObjectIdView object_id,
                                         std::function<void(Status)> callback) {
  FTL_DCHECK(page_sync_);
  FTL_DCHECK(GetObjectIdType(object_id) != ObjectIdType::INLINE);

  page_sync_->GetObject(object_id, [
    this, callback = std::move(callback), object_id = object_id.ToString()
  ](Status status, uint64_t size, mx::socket data) {
    if (status != Status::OK) {
      callback(status);
      return;
    }
    ReadDataSource(DataSource::Create(std::move(data), size), [
      this, callback = std::move(callback), object_id = std::move(object_id)
    ](Status status, std::unique_ptr<DataSource::DataChunk> chunk) {
      if (status != Status::OK) {
        callback(status);
        return;
      }

      auto object_id_type = GetObjectIdType(object_id);
      FTL_DCHECK(object_id_type == ObjectIdType::VALUE_HASH ||
                 object_id_type == ObjectIdType::INDEX_HASH);

      if (object_id !=
          ComputeObjectId(GetObjectType(object_id_type), chunk->Get())) {
        callback(Status::OBJECT_ID_MISMATCH);
        return;
      }

      if (object_id_type == ObjectIdType::VALUE_HASH) {
        AddPiece(std::move(object_id), std::move(chunk), ChangeSource::SYNC,
                 std::move(callback));
        return;
      }

      auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
      status = ForEachPiece(chunk->Get(), [&](ObjectIdView id) {
        if (GetObjectIdType(id) == ObjectIdType::INLINE) {
          return Status::OK;
        }

        auto id_string = id.ToString();
        Status status = db_.ReadObject(id_string, nullptr);
        if (status == Status::NOT_FOUND) {
          DownloadFullObject(std::move(id_string), waiter->NewCallback());
          return Status::OK;
        }
        return status;
      });
      if (status != Status::OK) {
        callback(status);
        return;
      }

      waiter->Finalize(ftl::MakeCopyable([
        this, object_id = std::move(object_id), chunk = std::move(chunk),
        callback = std::move(callback)
      ](Status status) mutable {
        if (status != Status::OK) {
          callback(status);
          return;
        }

        AddPiece(std::move(object_id), std::move(chunk), ChangeSource::SYNC,
                 std::move(callback));
      }));
    });

  });
}

void PageStorageImpl::GetObjectFromSync(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  if (!page_sync_) {
    callback(Status::NOT_CONNECTED_ERROR, nullptr);
    return;
  }

  DownloadFullObject(object_id, [
    this, object_id = object_id.ToString(), callback = std::move(callback)
  ](Status status) {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    GetObject(object_id, Location::LOCAL, std::move(callback));
  });
}

bool PageStorageImpl::ObjectIsUntracked(ObjectIdView object_id) {
  // TODO(qsr): Remove usage of this API, or makes it asynchronous.
  if (GetObjectIdType(object_id) == ObjectIdType::INLINE) {
    return false;
  }

  PageDb::ObjectStatus object_status;
  Status status = db_.GetObjectStatus(object_id, &object_status);
  FTL_DCHECK(status == Status::OK);
  return object_status == PageDb::ObjectStatus::TRANSIENT;
}

void PageStorageImpl::FillBufferWithObjectContent(
    ObjectIdView object_id,
    mx::vmo vmo,
    size_t offset,
    size_t size,
    std::function<void(Status)> callback) {
  GetPiece(object_id, ftl::MakeCopyable([
             this, vmo = std::move(vmo), offset, size,
             callback = std::move(callback)
           ](Status status, std::unique_ptr<const Object> object) {
             if (status != Status::OK) {
               callback(status);
               return;
             }

             FTL_DCHECK(object);
             ftl::StringView content;
             status = object->GetData(&content);
             if (status != Status::OK) {
               callback(status);
               return;
             }

             ObjectIdType id_type = GetObjectIdType(object->GetId());
             if (id_type == ObjectIdType::INLINE ||
                 id_type == ObjectIdType::VALUE_HASH) {
               if (size != content.size()) {
                 FTL_LOG(ERROR)
                     << "Error in serialization format. Expecting object: "
                     << convert::ToHex(object->GetId())
                     << " to have size: " << size
                     << ", but found an object of size: " << content.size();
                 callback(Status::FORMAT_ERROR);
                 return;
               }
               size_t written_size;
               mx_status_t mx_status =
                   vmo.write(content.data(), offset, size, &written_size);
               if (mx_status != MX_OK) {
                 FTL_LOG(ERROR)
                     << "Unable to write to vmo. Status: " << mx_status;
                 callback(Status::INTERNAL_IO_ERROR);
                 return;
               }
               if (written_size != size) {
                 FTL_LOG(ERROR)
                     << "Error when writing content to vmo. Expected to write:"
                     << size << " but only wrote: " << written_size;
                 callback(Status::INTERNAL_IO_ERROR);
                 return;
               }
               callback(Status::OK);
               return;
             }

             const FileIndex* file_index;
             status =
                 FileIndexSerialization::ParseFileIndex(content, &file_index);
             if (status != Status::OK) {
               callback(Status::FORMAT_ERROR);
               return;
             }
             if (file_index->size() != size) {
               FTL_LOG(ERROR)
                   << "Error in serialization format. Expecting object: "
                   << convert::ToHex(object->GetId())
                   << " to have size: " << size
                   << ", but found an index object of size: "
                   << file_index->size();
               callback(Status::FORMAT_ERROR);
               return;
             }

             size_t sub_offset = 0;
             auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
             for (const auto* child : *file_index->children()) {
               if (sub_offset + child->size() > file_index->size()) {
                 callback(Status::FORMAT_ERROR);
                 return;
               }
               mx::vmo vmo_copy;
               mx_status_t mx_status = vmo.duplicate(
                   MX_RIGHT_DUPLICATE | MX_RIGHT_WRITE, &vmo_copy);
               if (mx_status != MX_OK) {
                 FTL_LOG(ERROR)
                     << "Unable to duplicate vmo. Status: " << mx_status;
                 callback(Status::INTERNAL_IO_ERROR);
                 return;
               }
               FillBufferWithObjectContent(
                   child->object_id(), std::move(vmo_copy), offset + sub_offset,
                   child->size(), waiter->NewCallback());
               sub_offset += child->size();
             }
             waiter->Finalize(std::move(callback));
           }));
}

void PageStorageImpl::ReadDataSource(
    std::unique_ptr<DataSource> data_source,
    std::function<void(Status, std::unique_ptr<DataSource::DataChunk>)>
        callback) {
  auto handler = pending_operation_manager_.Manage(std::move(data_source));
  auto chunks = std::vector<std::unique_ptr<DataSource::DataChunk>>();
  (*handler.first)
      ->Get(ftl::MakeCopyable([
        cleanup = std::move(handler.second), chunks = std::move(chunks),
        callback = std::move(callback)
      ](std::unique_ptr<DataSource::DataChunk> chunk,
        DataSource::Status status) mutable {
        if (status == DataSource::Status::ERROR) {
          callback(Status::INTERNAL_IO_ERROR, nullptr);
          return;
        }

        if (chunk) {
          chunks.push_back(std::move(chunk));
        }

        if (status == DataSource::Status::TO_BE_CONTINUED) {
          return;
        }

        FTL_DCHECK(status == DataSource::Status::DONE);

        if (chunks.size() == 0) {
          callback(Status::OK, DataSource::DataChunk::Create(""));
          return;
        }

        if (chunks.size() == 1) {
          callback(Status::OK, std::move(chunks.front()));
          return;
        }

        size_t final_size = 0;
        for (const auto& chunk : chunks) {
          final_size += chunk->Get().size();
        }
        std::string final_content;
        final_content.reserve(final_size);
        for (const auto& chunk : chunks) {
          final_content.append(chunk->Get().data(), chunk->Get().size());
        }
        callback(Status::OK, DataSource::DataChunk::Create(final_content));
      }));
}

}  // namespace storage
