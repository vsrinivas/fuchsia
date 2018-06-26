// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"

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

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <trace/event.h>

#include "lib/callback/trace_callback.h"
#include "lib/callback/waiter.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"
#include "peridot/bin/ledger/lock/lock.h"
#include "peridot/bin/ledger/storage/impl/btree/diff.h"
#include "peridot/bin/ledger/storage/impl/btree/iterator.h"
#include "peridot/bin/ledger/storage/impl/commit_impl.h"
#include "peridot/bin/ledger/storage/impl/constants.h"
#include "peridot/bin/ledger/storage/impl/file_index.h"
#include "peridot/bin/ledger/storage/impl/file_index_generated.h"
#include "peridot/bin/ledger/storage/impl/journal_impl.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"
#include "peridot/bin/ledger/storage/impl/object_impl.h"
#include "peridot/bin/ledger/storage/impl/split.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {

using coroutine::CoroutineHandler;

namespace {

const char kLevelDbDir[] = "leveldb";

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

}  // namespace

PageStorageImpl::PageStorageImpl(
    async_t* async, coroutine::CoroutineService* coroutine_service,
    encryption::EncryptionService* encryption_service,
    ledger::DetachedPath page_dir, PageId page_id)
    : PageStorageImpl(
          async, coroutine_service, encryption_service,
          std::make_unique<PageDbImpl>(async, page_dir.SubPath(kLevelDbDir)),
          std::move(page_id)) {}

PageStorageImpl::PageStorageImpl(
    async_t* async, coroutine::CoroutineService* coroutine_service,
    encryption::EncryptionService* encryption_service,
    std::unique_ptr<PageDb> page_db, PageId page_id)
    : async_(async),
      coroutine_service_(coroutine_service),
      encryption_service_(encryption_service),
      page_id_(std::move(page_id)),
      db_(std::move(page_db)),
      page_sync_(nullptr) {}

PageStorageImpl::~PageStorageImpl() {
  // Interrupt any active handlers.
  while (!handlers_.empty()) {
    (*handlers_.begin())->Resume(coroutine::ContinuationStatus::INTERRUPTED);
  }
}

void PageStorageImpl::Init(std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, final_callback =
                 std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(SynchronousInit(handler));
      });
}

PageId PageStorageImpl::GetId() { return page_id_; }

void PageStorageImpl::SetSyncDelegate(PageSyncDelegate* page_sync) {
  page_sync_ = page_sync;
}

void PageStorageImpl::GetHeadCommitIds(
    std::function<void(Status, std::vector<CommitId>)> callback) {
  coroutine_service_->StartCoroutine(
      [this, final_callback =
                 std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        std::vector<CommitId> commit_ids;
        Status status = db_->GetHeads(handler, &commit_ids);
        callback(status, std::move(commit_ids));
      });
}

void PageStorageImpl::GetCommit(
    CommitIdView commit_id,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  FXL_DCHECK(commit_id.size());
  coroutine_service_->StartCoroutine([this, commit_id = commit_id.ToString(),
                                      final_callback = std::move(callback)](
                                         CoroutineHandler* handler) mutable {
    auto callback =
        UpdateActiveHandlersCallback(handler, std::move(final_callback));

    std::unique_ptr<const Commit> commit;
    Status status =
        SynchronousGetCommit(handler, std::move(commit_id), &commit);
    callback(status, std::move(commit));
  });
}

void PageStorageImpl::AddCommitFromLocal(
    std::unique_ptr<const Commit> commit,
    std::vector<ObjectIdentifier> new_objects,
    std::function<void(Status)> callback) {
  FXL_DCHECK(IsDigestValid(commit->GetRootIdentifier().object_digest));
  coroutine_service_->StartCoroutine(fxl::MakeCopyable(
      [this, commit = std::move(commit), new_objects = std::move(new_objects),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(SynchronousAddCommitFromLocal(handler, std::move(commit),
                                               std::move(new_objects)));
      }));
}

void PageStorageImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> ids_and_bytes, storage::ChangeSource source,
    std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      fxl::MakeCopyable([this, ids_and_bytes = std::move(ids_and_bytes), source,
                         final_callback = std::move(callback)](
                            CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));
        callback(SynchronousAddCommitsFromSync(
            handler, std::move(ids_and_bytes), source));
      }));
}

void PageStorageImpl::StartCommit(
    const CommitId& commit_id, JournalType journal_type,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  coroutine_service_->StartCoroutine(
      [this, commit_id, journal_type, final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        JournalId journal_id;
        Status status =
            db_->CreateJournalId(handler, journal_type, commit_id, &journal_id);
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }

        std::unique_ptr<Journal> journal = JournalImpl::Simple(
            journal_type, coroutine_service_, this, journal_id, commit_id);
        callback(Status::OK, std::move(journal));
      });
}

void PageStorageImpl::StartMergeCommit(
    const CommitId& left, const CommitId& right,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  coroutine_service_->StartCoroutine([this, left, right,
                                      final_callback = std::move(callback)](
                                         CoroutineHandler* handler) mutable {
    auto callback =
        UpdateActiveHandlersCallback(handler, std::move(final_callback));

    JournalId journal_id;
    Status status =
        db_->CreateJournalId(handler, JournalType::EXPLICIT, left, &journal_id);
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    std::unique_ptr<Journal> journal =
        JournalImpl::Merge(coroutine_service_, this, journal_id, left, right);
    callback(Status::OK, std::move(journal));
  });
}

void PageStorageImpl::CommitJournal(
    std::unique_ptr<Journal> journal,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  FXL_DCHECK(journal);

  auto managed_journal = managed_container_.Manage(std::move(journal));
  JournalImpl* journal_ptr = static_cast<JournalImpl*>(managed_journal->get());

  journal_ptr->Commit(fxl::MakeCopyable(
      [journal_ptr, managed_journal = std::move(managed_journal),
       callback = std::move(callback)](
          Status status, std::unique_ptr<const Commit> commit) mutable {
        if (status != Status::OK) {
          // Commit failed, roll the journal back.
          journal_ptr->Rollback(fxl::MakeCopyable(
              [status, managed_journal = std::move(managed_journal),
               callback = std::move(callback)](Status /*rollback_status*/) {
                callback(status, nullptr);
              }));
          return;
        }
        callback(Status::OK, std::move(commit));
      }));
}

void PageStorageImpl::RollbackJournal(std::unique_ptr<Journal> journal,
                                      std::function<void(Status)> callback) {
  FXL_DCHECK(journal);

  auto managed_journal = managed_container_.Manage(std::move(journal));
  JournalImpl* journal_ptr = static_cast<JournalImpl*>(managed_journal->get());

  journal_ptr->Rollback(fxl::MakeCopyable(
      [managed_journal = std::move(managed_journal),
       callback = std::move(callback)](Status status) { callback(status); }));
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

void PageStorageImpl::IsSynced(std::function<void(Status, bool)> callback) {
  auto waiter = fxl::MakeRefCounted<callback::Waiter<Status, bool>>(Status::OK);

  // Check for unsynced commits.
  coroutine_service_->StartCoroutine(
      [this, commits_callback =
                 waiter->NewCallback()](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(commits_callback));
        std::vector<CommitId> commit_ids;
        Status status = db_->GetUnsyncedCommitIds(handler, &commit_ids);
        if (status != Status::OK) {
          callback(status, false);
        } else {
          callback(Status::OK, commit_ids.empty());
        }
      });

  // Check for unsynced pieces.
  GetUnsyncedPieces([pieces_callback = waiter->NewCallback()](
                        Status status, std::vector<ObjectIdentifier> pieces) {
    if (status != Status::OK) {
      pieces_callback(status, false);
    } else {
      pieces_callback(Status::OK, pieces.empty());
    }
  });

  waiter->Finalize([callback = std::move(callback)](
                       Status status, std::vector<bool> is_synced) {
    if (status != Status::OK) {
      callback(status, false);
      return;
    }
    FXL_DCHECK(is_synced.size() == 2);
    callback(Status::OK, is_synced[0] && is_synced[1]);
  });
}

void PageStorageImpl::GetUnsyncedCommits(
    std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
        callback) {
  coroutine_service_->StartCoroutine(
      [this, final_callback =
                 std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));
        std::vector<std::unique_ptr<const Commit>> unsynced_commits;
        Status s = SynchronousGetUnsyncedCommits(handler, &unsynced_commits);
        callback(s, std::move(unsynced_commits));
      });
}

void PageStorageImpl::MarkCommitSynced(const CommitId& commit_id,
                                       std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, commit_id, final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(db_->MarkCommitIdSynced(handler, commit_id));
      });
}

void PageStorageImpl::GetUnsyncedPieces(
    std::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
  coroutine_service_->StartCoroutine([this,
                                      final_callback = std::move(callback)](
                                         CoroutineHandler* handler) mutable {
    auto callback =
        UpdateActiveHandlersCallback(handler, std::move(final_callback));

    std::vector<ObjectIdentifier> unsynced_object_identifiers;
    Status s = db_->GetUnsyncedPieces(handler, &unsynced_object_identifiers);
    callback(s, unsynced_object_identifiers);
  });
}

void PageStorageImpl::MarkPieceSynced(ObjectIdentifier object_identifier,
                                      std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, object_identifier = std::move(object_identifier),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(db_->SetObjectStatus(handler, object_identifier,
                                      PageDbObjectStatus::SYNCED));
      });
}

void PageStorageImpl::IsPieceSynced(
    ObjectIdentifier object_identifier,
    std::function<void(Status, bool)> callback) {
  coroutine_service_->StartCoroutine(
      [this, object_identifier = std::move(object_identifier),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        PageDbObjectStatus object_status;
        Status status =
            db_->GetObjectStatus(handler, object_identifier, &object_status);
        callback(status, object_status == PageDbObjectStatus::SYNCED);
      });
}

void PageStorageImpl::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    std::function<void(Status, ObjectIdentifier)> callback) {
  auto traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_add_object");

  auto managed_data_source = managed_container_.Manage(std::move(data_source));
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  SplitDataSource(
      managed_data_source->get(),
      fxl::MakeCopyable(
          [this, waiter, managed_data_source = std::move(managed_data_source),
           callback = std::move(traced_callback)](
              IterationStatus status, ObjectDigest object_digest,
              std::unique_ptr<DataSource::DataChunk> chunk) mutable {
            if (status == IterationStatus::ERROR) {
              callback(Status::IO_ERROR, ObjectIdentifier());
              return ObjectIdentifier();
            }
            FXL_DCHECK(IsDigestValid(object_digest));

            ObjectIdentifier identifier =
                encryption_service_->MakeObjectIdentifier(
                    std::move(object_digest));

            if (chunk) {
              FXL_DCHECK(status == IterationStatus::IN_PROGRESS);

              if (GetObjectDigestType(identifier.object_digest) !=
                  ObjectDigestType::INLINE) {
                AddPiece(identifier, std::move(chunk), ChangeSource::LOCAL,
                         waiter->NewCallback());
              }
              return identifier;
            }

            FXL_DCHECK(status == IterationStatus::DONE);
            waiter->Finalize(
                [identifier = std::move(identifier),
                 callback = std::move(callback)](Status status) mutable {
                  callback(status, std::move(identifier));
                });
            return identifier;
          }));
}

void PageStorageImpl::GetObject(
    ObjectIdentifier object_identifier, Location location,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  FXL_DCHECK(IsDigestValid(object_identifier.object_digest));
  GetPiece(
      object_identifier,
      [this, object_identifier, location, callback = std::move(callback)](
          Status status, std::unique_ptr<const Object> object) mutable {
        if (status == Status::NOT_FOUND) {
          if (location == Location::NETWORK) {
            GetObjectFromSync(object_identifier, std::move(callback));
          } else {
            callback(Status::NOT_FOUND, nullptr);
          }
          return;
        }

        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }

        FXL_DCHECK(object);
        ObjectDigestType digest_type =
            GetObjectDigestType(object_identifier.object_digest);

        if (digest_type == ObjectDigestType::INLINE ||
            digest_type == ObjectDigestType::VALUE_HASH) {
          callback(status, std::move(object));
          return;
        }

        FXL_DCHECK(digest_type == ObjectDigestType::INDEX_HASH);

        fxl::StringView content;
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

        zx::vmo raw_vmo;
        zx_status_t zx_status =
            zx::vmo::create(file_index->size(), 0, &raw_vmo);
        if (zx_status != ZX_OK) {
          callback(Status::INTERNAL_IO_ERROR, nullptr);
          return;
        }

        fsl::SizedVmo vmo(std::move(raw_vmo), file_index->size());
        size_t offset = 0;
        auto waiter =
            fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
        for (const auto* child : *file_index->children()) {
          if (offset + child->size() > file_index->size()) {
            callback(Status::FORMAT_ERROR, nullptr);
            return;
          }
          fsl::SizedVmo vmo_copy;
          zx_status_t zx_status =
              vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
          if (zx_status != ZX_OK) {
            FXL_LOG(ERROR) << "Unable to duplicate vmo. Status: " << zx_status;
            callback(Status::INTERNAL_IO_ERROR, nullptr);
            return;
          }
          FillBufferWithObjectContent(
              ToObjectIdentifier(child->object_identifier()),
              std::move(vmo_copy), offset, child->size(),
              waiter->NewCallback());
          offset += child->size();
        }
        if (offset != file_index->size()) {
          FXL_LOG(ERROR) << "Built file size doesn't add up.";
          callback(Status::FORMAT_ERROR, nullptr);
          return;
        }

        auto final_object = std::make_unique<VmoObject>(
            std::move(object_identifier), std::move(vmo));

        waiter->Finalize(fxl::MakeCopyable(
            [object = std::move(final_object),
             callback = std::move(callback)](Status status) mutable {
              callback(status, std::move(object));
            }));
      });
}

void PageStorageImpl::GetPiece(
    ObjectIdentifier object_identifier,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  ObjectDigestType digest_type =
      GetObjectDigestType(object_identifier.object_digest);
  if (digest_type == ObjectDigestType::INLINE) {
    callback(Status::OK,
             std::make_unique<InlinedObject>(std::move(object_identifier)));
    return;
  }

  coroutine_service_->StartCoroutine(
      [this, object_identifier = std::move(object_identifier),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        std::unique_ptr<const Object> object;
        Status status =
            db_->ReadObject(handler, std::move(object_identifier), &object);
        callback(status, std::move(object));
      });
}

void PageStorageImpl::SetSyncMetadata(fxl::StringView key,
                                      fxl::StringView value,
                                      std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, key = key.ToString(), value = value.ToString(),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(db_->SetSyncMetadata(handler, key, value));
      });
}

void PageStorageImpl::GetSyncMetadata(
    fxl::StringView key, std::function<void(Status, std::string)> callback) {
  coroutine_service_->StartCoroutine(
      [this, key = key.ToString(), final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        std::string value;
        Status status = db_->GetSyncMetadata(handler, key, &value);
        callback(status, std::move(value));
      });
}

void PageStorageImpl::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        std::function<bool(Entry)> on_next,
                                        std::function<void(Status)> on_done) {
  btree::ForEachEntry(
      coroutine_service_, this, commit.GetRootIdentifier(), min_key,
      [on_next = std::move(on_next)](btree::EntryAndNodeIdentifier next) {
        return on_next(next.entry);
      },
      std::move(on_done));
}

void PageStorageImpl::GetEntryFromCommit(
    const Commit& commit, std::string key,
    std::function<void(Status, Entry)> callback) {
  std::unique_ptr<bool> key_found = std::make_unique<bool>(false);
  auto on_next = [key, key_found = key_found.get(),
                  callback](btree::EntryAndNodeIdentifier next) {
    if (next.entry.key == key) {
      *key_found = true;
      callback(Status::OK, next.entry);
    }
    return false;
  };

  auto on_done =
      fxl::MakeCopyable([key_found = std::move(key_found),
                         callback = std::move(callback)](Status s) mutable {
        if (*key_found) {
          return;
        }
        if (s == Status::OK) {
          callback(Status::NOT_FOUND, Entry());
          return;
        }
        callback(s, Entry());
      });
  btree::ForEachEntry(coroutine_service_, this, commit.GetRootIdentifier(),
                      std::move(key), std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetCommitContentsDiff(
    const Commit& base_commit, const Commit& other_commit, std::string min_key,
    std::function<bool(EntryChange)> on_next_diff,
    std::function<void(Status)> on_done) {
  btree::ForEachDiff(coroutine_service_, this, base_commit.GetRootIdentifier(),
                     other_commit.GetRootIdentifier(), std::move(min_key),
                     std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::GetThreeWayContentsDiff(
    const Commit& base_commit, const Commit& left_commit,
    const Commit& right_commit, std::string min_key,
    std::function<bool(ThreeWayChange)> on_next_diff,
    std::function<void(Status)> on_done) {
  btree::ForEachThreeWayDiff(
      coroutine_service_, this, base_commit.GetRootIdentifier(),
      left_commit.GetRootIdentifier(), right_commit.GetRootIdentifier(),
      std::move(min_key), std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::GetJournalEntries(
    const JournalId& journal_id,
    std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
        callback) {
  coroutine_service_->StartCoroutine(
      [this, journal_id, final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        std::unique_ptr<Iterator<const EntryChange>> entries;
        Status s = db_->GetJournalEntries(handler, journal_id, &entries);
        callback(s, std::move(entries));
      });
}

void PageStorageImpl::AddJournalEntry(const JournalId& journal_id,
                                      fxl::StringView key,
                                      ObjectIdentifier object_identifier,
                                      KeyPriority priority,
                                      std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, journal_id, key = key.ToString(),
       object_identifier = std::move(object_identifier), priority,
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(db_->AddJournalEntry(handler, journal_id, key,
                                      object_identifier, priority));
      });
}

void PageStorageImpl::RemoveJournalEntry(const JournalId& journal_id,
                                         convert::ExtendedStringView key,
                                         std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine([this, journal_id, key = key.ToString(),
                                      final_callback = std::move(callback)](
                                         CoroutineHandler* handler) mutable {
    auto callback =
        UpdateActiveHandlersCallback(handler, std::move(final_callback));

    callback(db_->RemoveJournalEntry(handler, journal_id, key));
  });
}

void PageStorageImpl::RemoveJournal(const JournalId& journal_id,
                                    std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(
      [this, journal_id, final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        callback(db_->RemoveJournal(handler, journal_id));
      });
}

void PageStorageImpl::NotifyWatchers() {
  while (!commits_to_send_.empty()) {
    auto to_send = std::move(commits_to_send_.front());
    for (CommitWatcher* watcher : watchers_) {
      watcher->OnNewCommits(to_send.second, to_send.first);
    }
    commits_to_send_.pop();
  }
}

Status PageStorageImpl::MarkAllPiecesLocal(
    CoroutineHandler* handler, PageDb::Batch* batch,
    std::vector<ObjectIdentifier> object_identifiers) {
  std::set<ObjectIdentifier> seen_identifiers;
  while (!object_identifiers.empty()) {
    auto it = seen_identifiers.insert(std::move(object_identifiers.back()));
    object_identifiers.pop_back();
    const ObjectIdentifier& object_identifier = *(it.first);
    FXL_DCHECK(GetObjectDigestType(object_identifier.object_digest) !=
               ObjectDigestType::INLINE);
    Status status = batch->SetObjectStatus(handler, object_identifier,
                                           PageDbObjectStatus::LOCAL);
    if (status != Status::OK) {
      return status;
    }
    if (GetObjectDigestType(object_identifier.object_digest) ==
        ObjectDigestType::INDEX_HASH) {
      std::unique_ptr<const Object> object;
      status = db_->ReadObject(handler, object_identifier, &object);
      if (status != Status::OK) {
        return status;
      }

      fxl::StringView content;
      status = object->GetData(&content);
      if (status != Status::OK) {
        return status;
      }

      const FileIndex* file_index;
      status = FileIndexSerialization::ParseFileIndex(content, &file_index);
      if (status != Status::OK) {
        return status;
      }

      object_identifiers.reserve(object_identifiers.size() +
                                 file_index->children()->size());
      for (const auto* child : *file_index->children()) {
        if (GetObjectDigestType(child->object_identifier()->object_digest()) !=
            ObjectDigestType::INLINE) {
          ObjectIdentifier new_object_identifier =
              ToObjectIdentifier(child->object_identifier());
          if (!seen_identifiers.count(new_object_identifier)) {
            object_identifiers.push_back(std::move(new_object_identifier));
          }
        }
      }
    }
  }
  return Status::OK;
}

Status PageStorageImpl::ContainsCommit(CoroutineHandler* handler,
                                       CommitIdView id) {
  if (IsFirstCommit(id)) {
    return Status::OK;
  }
  std::string bytes;
  return db_->GetCommitStorageBytes(handler, id, &bytes);
}

bool PageStorageImpl::IsFirstCommit(CommitIdView id) {
  return id == kFirstPageCommitId;
}

void PageStorageImpl::AddPiece(ObjectIdentifier object_identifier,
                               std::unique_ptr<DataSource::DataChunk> data,
                               ChangeSource source,
                               std::function<void(Status)> callback) {
  coroutine_service_->StartCoroutine(fxl::MakeCopyable(
      [this, object_identifier = std::move(object_identifier),
       data = std::move(data), source, final_callback = std::move(callback)](
          CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));
        callback(SynchronousAddPiece(handler, std::move(object_identifier),
                                     std::move(data), source));
      }));
}

void PageStorageImpl::DownloadFullObject(ObjectIdentifier object_identifier,
                                         std::function<void(Status)> callback) {
  FXL_DCHECK(page_sync_);
  FXL_DCHECK(GetObjectDigestType(object_identifier.object_digest) !=
             ObjectDigestType::INLINE);

  page_sync_->GetObject(
      object_identifier,
      [this, callback = std::move(callback),
       object_identifier = std::move(object_identifier)](
          Status status, ChangeSource source,
          std::unique_ptr<DataSource::DataChunk> chunk) mutable {
        if (status != Status::OK) {
          callback(status);
          return;
        }
        if (status != Status::OK) {
          callback(status);
          return;
        }
        coroutine_service_->StartCoroutine(fxl::MakeCopyable(
            [this, object_identifier = std::move(object_identifier), source,
             chunk = std::move(chunk), final_callback = std::move(callback)](
                CoroutineHandler* handler) mutable {
              auto callback = UpdateActiveHandlersCallback(
                  handler, std::move(final_callback));

              auto object_digest_type =
                  GetObjectDigestType(object_identifier.object_digest);
              FXL_DCHECK(object_digest_type == ObjectDigestType::VALUE_HASH ||
                         object_digest_type == ObjectDigestType::INDEX_HASH);

              if (object_identifier.object_digest !=
                  ComputeObjectDigest(GetObjectType(object_digest_type),
                                      chunk->Get())) {
                callback(Status::OBJECT_DIGEST_MISMATCH);
                return;
              }

              if (object_digest_type == ObjectDigestType::VALUE_HASH) {
                AddPiece(std::move(object_identifier), std::move(chunk), source,
                         std::move(callback));
                return;
              }

              auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(
                  Status::OK);
              Status status =
                  ForEachPiece(chunk->Get(), [&](ObjectIdentifier identifier) {
                    if (GetObjectDigestType(identifier.object_digest) ==
                        ObjectDigestType::INLINE) {
                      return Status::OK;
                    }

                    Status status =
                        db_->ReadObject(handler, identifier, nullptr);
                    if (status == Status::NOT_FOUND) {
                      DownloadFullObject(std::move(identifier),
                                         waiter->NewCallback());
                      return Status::OK;
                    }
                    return status;
                  });
              if (status != Status::OK) {
                callback(status);
                return;
              }

              waiter->Finalize(fxl::MakeCopyable(
                  [this, object_identifier = std::move(object_identifier),
                   source, chunk = std::move(chunk),
                   callback = std::move(callback)](Status status) mutable {
                    if (status != Status::OK) {
                      callback(status);
                      return;
                    }

                    AddPiece(std::move(object_identifier), std::move(chunk),
                             source, std::move(callback));
                  }));
            }));
      });
}

void PageStorageImpl::GetObjectFromSync(
    ObjectIdentifier object_identifier,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  if (!page_sync_) {
    callback(Status::NOT_CONNECTED_ERROR, nullptr);
    return;
  }

  DownloadFullObject(object_identifier, [this, object_identifier,
                                         callback = std::move(callback)](
                                            Status status) mutable {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    GetObject(object_identifier, Location::LOCAL, std::move(callback));
  });
}

void PageStorageImpl::ObjectIsUntracked(
    ObjectIdentifier object_identifier,
    std::function<void(Status, bool)> callback) {
  coroutine_service_->StartCoroutine(
      [this, object_identifier = std::move(object_identifier),
       final_callback =
           std::move(callback)](CoroutineHandler* handler) mutable {
        auto callback =
            UpdateActiveHandlersCallback(handler, std::move(final_callback));

        if (GetObjectDigestType(object_identifier.object_digest) ==
            ObjectDigestType::INLINE) {
          callback(Status::OK, false);
          return;
        }

        PageDbObjectStatus object_status;
        Status status =
            db_->GetObjectStatus(handler, object_identifier, &object_status);
        callback(status, object_status == PageDbObjectStatus::TRANSIENT);
      });
}

void PageStorageImpl::FillBufferWithObjectContent(
    ObjectIdentifier object_identifier, fsl::SizedVmo vmo, size_t offset,
    size_t size, std::function<void(Status)> callback) {
  GetPiece(
      object_identifier,
      fxl::MakeCopyable([this, vmo = std::move(vmo), offset, size,
                         callback = std::move(callback)](
                            Status status,
                            std::unique_ptr<const Object> object) mutable {
        if (status != Status::OK) {
          callback(status);
          return;
        }

        FXL_DCHECK(object);
        fxl::StringView content;
        status = object->GetData(&content);
        if (status != Status::OK) {
          callback(status);
          return;
        }

        ObjectDigestType digest_type =
            GetObjectDigestType(object->GetIdentifier().object_digest);
        if (digest_type == ObjectDigestType::INLINE ||
            digest_type == ObjectDigestType::VALUE_HASH) {
          if (size != content.size()) {
            FXL_LOG(ERROR)
                << "Error in serialization format. Expecting object: "
                << object->GetIdentifier() << " to have size: " << size
                << ", but found an object of size: " << content.size();
            callback(Status::FORMAT_ERROR);
            return;
          }
          zx_status_t zx_status = vmo.vmo().write(content.data(), offset, size);
          if (zx_status != ZX_OK) {
            FXL_LOG(ERROR) << "Unable to write to vmo. Status: " << zx_status;
            callback(Status::INTERNAL_IO_ERROR);
            return;
          }
          callback(Status::OK);
          return;
        }

        const FileIndex* file_index;
        status = FileIndexSerialization::ParseFileIndex(content, &file_index);
        if (status != Status::OK) {
          callback(Status::FORMAT_ERROR);
          return;
        }
        if (file_index->size() != size) {
          FXL_LOG(ERROR) << "Error in serialization format. Expecting object: "
                         << object->GetIdentifier() << " to have size: " << size
                         << ", but found an index object of size: "
                         << file_index->size();
          callback(Status::FORMAT_ERROR);
          return;
        }

        size_t sub_offset = 0;
        auto waiter =
            fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
        for (const auto* child : *file_index->children()) {
          if (sub_offset + child->size() > file_index->size()) {
            callback(Status::FORMAT_ERROR);
            return;
          }
          fsl::SizedVmo vmo_copy;
          zx_status_t zx_status =
              vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
          if (zx_status != ZX_OK) {
            FXL_LOG(ERROR) << "Unable to duplicate vmo. Status: " << zx_status;
            callback(Status::INTERNAL_IO_ERROR);
            return;
          }
          FillBufferWithObjectContent(
              ToObjectIdentifier(child->object_identifier()),
              std::move(vmo_copy), offset + sub_offset, child->size(),
              waiter->NewCallback());
          sub_offset += child->size();
        }
        waiter->Finalize(std::move(callback));
      }));
}

Status PageStorageImpl::SynchronousInit(CoroutineHandler* handler) {
  // Initialize PageDb.
  Status s = db_->Init();
  if (s != Status::OK) {
    return s;
  }

  // Add the default page head if this page is empty.
  std::vector<CommitId> heads;
  s = db_->GetHeads(handler, &heads);
  if (s != Status::OK) {
    return s;
  }
  if (heads.empty()) {
    s = db_->AddHead(handler, kFirstPageCommitId, 0);
    if (s != Status::OK) {
      return s;
    }
  }

  // Remove uncommited explicit journals.
  if (db_->RemoveExplicitJournals(handler) == Status::INTERRUPTED) {
    // Only fail if the handler is invalidated. Otherwise, failure to remove
    // explicit journals should not block the initalization.
    return Status::INTERRUPTED;
  }

  // Commit uncommited implicit journals.
  std::vector<JournalId> journal_ids;
  s = db_->GetImplicitJournalIds(handler, &journal_ids);
  if (s != Status::OK) {
    return s;
  }

  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  for (JournalId& id : journal_ids) {
    CommitId base;
    s = db_->GetBaseCommitForJournal(handler, id, &base);
    if (s != Status::OK) {
      FXL_LOG(ERROR) << "Failed to get implicit journal with status " << s
                     << ". journal id: " << id;
      return s;
    }
    std::unique_ptr<Journal> journal = JournalImpl::Simple(
        JournalType::IMPLICIT, coroutine_service_, this, id, base);

    CommitJournal(
        std::move(journal), [status_callback = waiter->NewCallback()](
                                Status status, std::unique_ptr<const Commit>) {
          if (status != Status::OK) {
            FXL_LOG(ERROR) << "Failed to commit implicit journal created in "
                              "previous Ledger execution.";
          }
          status_callback(status);
        });
  }

  if (coroutine::SyncCall(
          handler,
          [waiter = std::move(waiter)](std::function<void(Status)> callback) {
            waiter->Finalize(std::move(callback));
          },
          &s) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return s;
}

Status PageStorageImpl::SynchronousGetCommit(
    CoroutineHandler* handler, CommitId commit_id,
    std::unique_ptr<const Commit>* commit) {
  if (IsFirstCommit(commit_id)) {
    Status s;
    if (coroutine::SyncCall(
            handler,
            [this](std::function<void(Status, std::unique_ptr<const Commit>)>
                       callback) {
              CommitImpl::Empty(this, std::move(callback));
            },
            &s, commit) == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    return s;
  }
  std::string bytes;
  Status s = db_->GetCommitStorageBytes(handler, commit_id, &bytes);
  if (s != Status::OK) {
    return s;
  }
  return CommitImpl::FromStorageBytes(this, commit_id, std::move(bytes),
                                      commit);
}

Status PageStorageImpl::SynchronousAddCommitFromLocal(
    CoroutineHandler* handler, std::unique_ptr<const Commit> commit,
    std::vector<ObjectIdentifier> new_objects) {
  std::vector<std::unique_ptr<const Commit>> commits;
  commits.reserve(1);
  commits.push_back(std::move(commit));

  return SynchronousAddCommits(handler, std::move(commits), ChangeSource::LOCAL,
                               std::move(new_objects));
}

Status PageStorageImpl::SynchronousAddCommitsFromSync(
    CoroutineHandler* handler, std::vector<CommitIdAndBytes> ids_and_bytes,
    ChangeSource source) {
  std::vector<std::unique_ptr<const Commit>> commits;

  std::map<const CommitId*, const Commit*, StringPointerComparator> leaves;
  commits.reserve(ids_and_bytes.size());

  // The locked section below contains asynchronous operations reading the
  // database, and branches depending on those reads. This section is thus a
  // critical section and we need to ensure it is not executed concurrently by
  // several coroutines. The locked sections (and only those) are thus executed
  // serially.
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &commit_serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }

  for (auto& id_and_bytes : ids_and_bytes) {
    CommitId id = std::move(id_and_bytes.id);
    std::string storage_bytes = std::move(id_and_bytes.bytes);
    Status status = ContainsCommit(handler, id);
    if (status == Status::OK) {
      // We only mark cloud-sourced commits as synced.
      if (source == ChangeSource::CLOUD) {
        Status status = SynchronousMarkCommitSynced(handler, id);
        if (status != Status::OK) {
          return status;
        }
      }
      continue;
    }

    if (status != Status::NOT_FOUND) {
      return status;
    }

    std::unique_ptr<const Commit> commit;
    status = CommitImpl::FromStorageBytes(this, id, std::move(storage_bytes),
                                          &commit);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "Unable to add commit. Id: " << convert::ToHex(id);
      return status;
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
    return Status::OK;
  }

  lock.reset();

  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  // Get all objects from sync and then add the commit objects.
  for (const auto& leaf : leaves) {
    btree::GetObjectsFromSync(coroutine_service_, this,
                              leaf.second->GetRootIdentifier(),
                              waiter->NewCallback());
  }

  Status waiter_status;
  if (coroutine::SyncCall(
          handler,
          [waiter = std::move(waiter)](std::function<void(Status)> callback) {
            waiter->Finalize(std::move(callback));
          },
          &waiter_status) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (waiter_status != Status::OK) {
    return waiter_status;
  }

  return SynchronousAddCommits(handler, std::move(commits), source,
                               std::vector<ObjectIdentifier>());
}

Status PageStorageImpl::SynchronousGetUnsyncedCommits(
    CoroutineHandler* handler,
    std::vector<std::unique_ptr<const Commit>>* unsynced_commits) {
  std::vector<CommitId> commit_ids;
  Status s = db_->GetUnsyncedCommitIds(handler, &commit_ids);
  if (s != Status::OK) {
    return s;
  }

  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<Status, std::unique_ptr<const Commit>>>(Status::OK);
  for (const auto& commit_id : commit_ids) {
    GetCommit(commit_id, waiter->NewCallback());
  }

  std::vector<std::unique_ptr<const Commit>> result;
  if (coroutine::SyncCall(
          handler,
          [waiter = std::move(waiter)](
              std::function<void(Status,
                                 std::vector<std::unique_ptr<const Commit>>)>
                  callback) { waiter->Finalize(std::move(callback)); },
          &s, &result) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (s != Status::OK) {
    return s;
  }
  unsynced_commits->swap(result);
  return Status::OK;
}

Status PageStorageImpl::SynchronousMarkCommitSynced(CoroutineHandler* handler,
                                                    const CommitId& commit_id) {
  return db_->MarkCommitIdSynced(handler, commit_id);
}

Status PageStorageImpl::SynchronousAddCommits(
    CoroutineHandler* handler,
    std::vector<std::unique_ptr<const Commit>> commits, ChangeSource source,
    std::vector<ObjectIdentifier> new_objects) {
  // Make sure that only one AddCommits operation is executed at a time.
  // Otherwise, if db_ operations are asynchronous, ContainsCommit (below) may
  // return NOT_FOUND while another commit is added, and batch->Execute() will
  // break the invariants of this system (in particular, that synced commits
  // cannot become unsynced).
  std::unique_ptr<lock::Lock> lock;
  if (lock::AcquireLock(handler, &commit_serializer_, &lock) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }

  // Apply all changes atomically.
  std::unique_ptr<PageDb::Batch> batch;
  Status status = db_->StartBatch(handler, &batch);
  if (status != Status::OK) {
    return status;
  }
  std::set<const CommitId*, StringPointerComparator> added_commits;
  std::vector<std::unique_ptr<const Commit>> commits_to_send;

  std::map<CommitId, int64_t> heads_to_add;

  // If commits arrive out of order, some commits might be skipped. Continue
  // trying adding commits as long as at least one commit is added on each
  // iteration.
  bool commits_were_out_of_order = false;
  bool continue_trying = true;
  while (continue_trying && !commits.empty()) {
    continue_trying = false;
    std::vector<std::unique_ptr<const Commit>> remaining_commits;

    for (auto& commit : commits) {
      // We need to check if we are adding an already present remote commit here
      // because we might both download and locally commit the same commit at
      // roughly the same time. As commit writing is asynchronous, the previous
      // check in AddCommitsFromSync may have not matched any commit, while a
      // commit got added in between.
      Status s = ContainsCommit(handler, commit->GetId());
      if (s == Status::OK) {
        if (source == ChangeSource::CLOUD) {
          s = batch->MarkCommitIdSynced(handler, commit->GetId());
          if (s != Status::OK) {
            return s;
          }
        }
        // The commit is already here. We can safely skip it.
        continue;
      }
      if (s != Status::NOT_FOUND) {
        return s;
      }
      // Now, we know we are adding a new commit.

      // Commits should arrive in order. Check that the parents are either
      // present in PageDb or in the list of already processed commits.
      // If the commit arrive out of order, print an error, but skip it
      // temporarly so that the Ledger can recover if all the needed commits
      // are received in a single batch.
      for (const CommitIdView& parent_id : commit->GetParentIds()) {
        if (added_commits.count(&parent_id) == 0) {
          s = ContainsCommit(handler, parent_id);
          if (s != Status::OK) {
            FXL_LOG(ERROR) << "Failed to find parent commit \""
                           << ToHex(parent_id) << "\" of commit \""
                           << convert::ToHex(commit->GetId())
                           << "\". Temporarily skipping in case the commits "
                              "are out of order.";
            if (s == Status::NOT_FOUND) {
              remaining_commits.push_back(std::move(commit));
              commit.reset();
              break;
            }
            return Status::INTERNAL_IO_ERROR;
          }
        }
        // Remove the parent from the list of heads.
        if (!heads_to_add.erase(parent_id.ToString())) {
          // parent_id was not added in the batch: remove it from heads in Db.
          s = batch->RemoveHead(handler, parent_id);
          if (s != Status::OK) {
            return s;
          }
        }
      }

      // The commit could not be added. Skip it.
      if (!commit) {
        continue;
      }

      continue_trying = true;

      s = batch->AddCommitStorageBytes(handler, commit->GetId(),
                                       commit->GetStorageBytes());
      if (s != Status::OK) {
        return s;
      }

      if (source != ChangeSource::CLOUD) {
        s = batch->MarkCommitIdUnsynced(handler, commit->GetId(),
                                        commit->GetGeneration());
        if (s != Status::OK) {
          return s;
        }
      }

      // Update heads_to_add.
      heads_to_add[commit->GetId()] = commit->GetTimestamp();

      added_commits.insert(&commit->GetId());
      commits_to_send.push_back(std::move(commit));
    }

    if (!remaining_commits.empty()) {
      // If |remaining_commits| is not empty, some commits were out of order.
      commits_were_out_of_order = true;
    }
    // Update heads in Db.
    for (const auto& head_timestamp : heads_to_add) {
      Status s =
          batch->AddHead(handler, head_timestamp.first, head_timestamp.second);
      if (s != Status::OK) {
        return s;
      }
    }
    std::swap(commits, remaining_commits);
  }

  if (commits_were_out_of_order) {
    ledger::ReportEvent(ledger::CobaltEvent::COMMITS_RECEIVED_OUT_OF_ORDER);
  }
  if (!commits.empty()) {
    FXL_DCHECK(commits_were_out_of_order);
    ledger::ReportEvent(
        ledger::CobaltEvent::COMMITS_RECEIVED_OUT_OF_ORDER_NOT_RECOVERED);
    FXL_LOG(ERROR) << "Failed adding commits. Found " << commits.size()
                   << " orphaned commits.";
    return Status::ILLEGAL_STATE;
  }

  // If adding local commits, mark all new pieces as local.
  Status s = MarkAllPiecesLocal(handler, batch.get(), std::move(new_objects));
  if (s != Status::OK) {
    return s;
  }

  s = batch->Execute(handler);

  // TODO(nellyv): we can probably remove commits_to_send_ and send
  // commits_to_send directly to NotifyWatchers(). See LE-320.
  FXL_DCHECK(commits_to_send_.empty());
  commits_to_send_.emplace(source, std::move(commits_to_send));
  NotifyWatchers();

  return s;
}

Status PageStorageImpl::SynchronousAddPiece(
    CoroutineHandler* handler, ObjectIdentifier object_identifier,
    std::unique_ptr<DataSource::DataChunk> data, ChangeSource source) {
  FXL_DCHECK(GetObjectDigestType(object_identifier.object_digest) !=
             ObjectDigestType::INLINE);
  FXL_DCHECK(object_identifier.object_digest ==
             ComputeObjectDigest(GetObjectType(GetObjectDigestType(
                                     object_identifier.object_digest)),
                                 data->Get()));

  std::unique_ptr<const Object> object;
  Status status = db_->ReadObject(handler, object_identifier, &object);
  if (status == Status::NOT_FOUND) {
    PageDbObjectStatus object_status =
        (source == ChangeSource::LOCAL ? PageDbObjectStatus::TRANSIENT
                                       : PageDbObjectStatus::SYNCED);
    return db_->WriteObject(handler, object_identifier, std::move(data),
                            object_status);
  }
  return status;
}

}  // namespace storage
