// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_storage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/status.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "lib/async/cpp/task.h"
#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/impl/btree/builder.h"
#include "src/ledger/bin/storage/impl/btree/diff.h"
#include "src/ledger/bin/storage/impl/btree/iterator.h"
#include "src/ledger/bin/storage/impl/commit_factory.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/split.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/synchronization/lock.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/trace_callback.h"
#include "src/lib/fxl/arraysize.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {

using ::coroutine::CoroutineHandler;

struct StringPointerComparator {
  using is_transparent = std::true_type;

  bool operator()(const std::string* str1, const std::string* str2) const { return *str1 < *str2; }

  bool operator()(const std::string* str1, const CommitIdView* str2) const { return *str1 < *str2; }

  bool operator()(const CommitIdView* str1, const std::string* str2) const { return *str1 < *str2; }
};

// Converts the user-provided offset for an object part (defined in comments for
// |FetchPartial| in ledger.fidl) to the actual offset used for reading. If the
// offset is off-limits, returns the |object_size|.
int64_t GetObjectPartStart(int64_t offset, int64_t object_size) {
  // Valid indices are between -N and N-1.
  if (offset < -object_size || offset >= object_size) {
    return object_size;
  }
  return offset < 0 ? object_size + offset : offset;
}

int64_t GetObjectPartLength(int64_t max_size, int64_t object_size, int64_t start) {
  int64_t adjusted_max_size = max_size < 0 ? object_size : max_size;
  return start > object_size ? 0 : std::min(adjusted_max_size, object_size - start);
}

ObjectIdentifierFactoryImpl::NotificationPolicy ToObjectIdentifierPolicy(
    GarbageCollectionPolicy policy) {
  switch (policy) {
    case GarbageCollectionPolicy::NEVER:
      return ObjectIdentifierFactoryImpl::NotificationPolicy::NEVER;
    case GarbageCollectionPolicy::EAGER_LIVE_REFERENCES:
      return ObjectIdentifierFactoryImpl::NotificationPolicy::ALWAYS;
    case GarbageCollectionPolicy::EAGER_ROOT_NODES:
      return ObjectIdentifierFactoryImpl::NotificationPolicy::ON_MARKED_OBJECTS_ONLY;
  }
}

}  // namespace

PageStorageImpl::PageStorageImpl(ledger::Environment* environment,
                                 encryption::EncryptionService* encryption_service,
                                 std::unique_ptr<Db> db, PageId page_id, CommitPruningPolicy policy)
    : PageStorageImpl(
          environment, encryption_service,
          std::make_unique<PageDbImpl>(environment, &object_identifier_factory_, std::move(db)),
          std::move(page_id), policy) {}

PageStorageImpl::PageStorageImpl(ledger::Environment* environment,
                                 encryption::EncryptionService* encryption_service,
                                 std::unique_ptr<PageDb> page_db, PageId page_id,
                                 CommitPruningPolicy policy)
    : environment_(environment),
      encryption_service_(encryption_service),
      page_id_(std::move(page_id)),
      object_identifier_factory_(ToObjectIdentifierPolicy(environment_->gc_policy())),
      commit_factory_(&object_identifier_factory_),
      db_(std::move(page_db)),
      commit_pruner_(environment, this, &commit_factory_, policy),
      page_sync_(nullptr),
      download_manager_(environment->coroutine_service(),
                        /*max_coroutines=*/kMaxConcurrentDownloads),
      coroutine_manager_(environment->coroutine_service()),
      weak_factory_(this) {}

PageStorageImpl::~PageStorageImpl() = default;

void PageStorageImpl::Init(clocks::DeviceIdManager* device_id_manager,
                           fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, device_id_manager](CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(SynchronousInit(handler, device_id_manager));
      });
}

PageId PageStorageImpl::GetId() { return page_id_; }

void PageStorageImpl::SetSyncDelegate(PageSyncDelegate* page_sync) { page_sync_ = page_sync; }

Status PageStorageImpl::GetHeadCommits(std::vector<std::unique_ptr<const Commit>>* head_commits) {
  LEDGER_DCHECK(head_commits);
  *head_commits = commit_factory_.GetHeads();
  return Status::OK;
}

void PageStorageImpl::GetMergeCommitIds(
    CommitIdView parent1_id, CommitIdView parent2_id,
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, parent1_id = convert::ToString(parent1_id),
       parent2_id = convert::ToString(parent2_id)](
          CoroutineHandler* handler, fit::function<void(Status, std::vector<CommitId>)> callback) {
        std::vector<CommitId> commit_ids;
        Status status = db_->GetMerges(handler, parent1_id, parent2_id, &commit_ids);
        callback(status, std::move(commit_ids));
      });
}

void PageStorageImpl::GetCommit(
    CommitIdView commit_id, fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  LEDGER_DCHECK(commit_id.size());
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, commit_id = convert::ToString(commit_id)](
          CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
        std::unique_ptr<const Commit> commit;
        Status status = SynchronousGetCommit(handler, commit_id, &commit);
        callback(status, std::move(commit));
      });
}

void PageStorageImpl::GetGenerationAndMissingParents(
    const CommitIdAndBytes& id_and_bytes,
    fit::function<void(Status, uint64_t, std::vector<CommitId>)> callback) {
  std::unique_ptr<const Commit> commit;
  Status status = commit_factory_.FromStorageBytes(std::move(id_and_bytes.id),
                                                   std::move(id_and_bytes.bytes), &commit);
  if (status != Status::OK) {
    LEDGER_LOG(ERROR) << "Unable to load commit from storage bytes.";
    callback(status, 0, {});
    return;
  }

  auto waiter = ledger::MakeRefCounted<ledger::StatusWaiter<Status>>(Status::OK);

  // The vector must not move until the finalizer is called.
  auto result = std::make_unique<std::vector<CommitId>>();
  auto result_ptr = result.get();

  for (CommitIdView parent_id : commit->GetParentIds()) {
    GetCommit(parent_id,
              waiter->MakeScoped([callback = waiter->NewCallback(), parent_id, result_ptr](
                                     Status status, std::unique_ptr<const Commit> /*commit*/) {
                if (status == Status::INTERNAL_NOT_FOUND) {
                  // |result| is alive, because |ledger::Waiter::MakeScoped| only calls us if the
                  // finalizer has not run yet.
                  result_ptr->push_back(convert::ToString(parent_id));
                  callback(Status::OK);
                  return;
                }
                callback(status);
              }));
  }

  waiter->Finalize([commit = std::move(commit), callback = std::move(callback),
                    result = std::move(result)](Status status) {
    if (status != Status::OK) {
      callback(status, 0, {});
      return;
    }
    callback(Status::OK, commit->GetGeneration(), std::move(*result));
  });
}

void PageStorageImpl::AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                                         ChangeSource source,
                                         fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, ids_and_bytes = std::move(ids_and_bytes), source](
          CoroutineHandler* handler, fit::function<void(Status)> callback) mutable {
        Status status = SynchronousAddCommitsFromSync(handler, std::move(ids_and_bytes), source);
        callback(status);
      });
}

std::unique_ptr<Journal> PageStorageImpl::StartCommit(std::unique_ptr<const Commit> commit) {
  return JournalImpl::Simple(environment_, this, std::move(commit));
}

std::unique_ptr<Journal> PageStorageImpl::StartMergeCommit(std::unique_ptr<const Commit> left,
                                                           std::unique_ptr<const Commit> right) {
  return JournalImpl::Merge(environment_, this, std::move(left), std::move(right));
}

void PageStorageImpl::CommitJournal(
    std::unique_ptr<Journal> journal,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  LEDGER_DCHECK(journal);

  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, journal = std::move(journal)](
          CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<const Commit>)> callback) mutable {
        JournalImpl* journal_ptr = static_cast<JournalImpl*>(journal.get());

        std::unique_ptr<const Commit> commit;
        std::vector<ObjectIdentifier> objects_to_sync;
        Status status = journal_ptr->Commit(handler, &commit, &objects_to_sync);
        if (status != Status::OK || !commit) {
          // There is an error, or the commit is empty (no change).
          callback(status, nullptr);
          return;
        }

        status =
            SynchronousAddCommitFromLocal(handler, commit->Clone(), std::move(objects_to_sync));

        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }

        callback(status, std::move(commit));
      });
}

void PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) { watchers_.AddObserver(watcher); }

void PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  watchers_.RemoveObserver(watcher);
}

void PageStorageImpl::IsSynced(fit::function<void(Status, bool)> callback) {
  auto waiter = ledger::MakeRefCounted<ledger::Waiter<Status, bool>>(Status::OK);
  // Check for unsynced commits.
  coroutine_manager_.StartCoroutine(
      waiter->NewCallback(),
      [this](CoroutineHandler* handler, fit::function<void(Status, bool)> callback) {
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

  waiter->Finalize([callback = std::move(callback)](Status status, std::vector<bool> is_synced) {
    if (status != Status::OK) {
      callback(status, false);
      return;
    }
    LEDGER_DCHECK(is_synced.size() == 2);
    callback(Status::OK, is_synced[0] && is_synced[1]);
  });
}

bool PageStorageImpl::IsOnline() { return page_is_online_; }

void PageStorageImpl::IsEmpty(fit::function<void(Status, bool)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](CoroutineHandler* handler, fit::function<void(Status, bool)> callback) {
        // Check there is a single head.
        std::vector<std::pair<zx::time_utc, CommitId>> commit_ids;
        Status status = db_->GetHeads(handler, &commit_ids);
        if (status != Status::OK) {
          callback(status, false);
          return;
        }
        LEDGER_DCHECK(!commit_ids.empty());
        if (commit_ids.size() > 1) {
          // A page is not empty if there is more than one head commit.
          callback(Status::OK, false);
          return;
        }
        // Compare the root node of the head commit to that of the empty node.
        std::unique_ptr<const Commit> commit;
        status = SynchronousGetCommit(handler, commit_ids[0].second, &commit);
        if (status != Status::OK) {
          callback(status, false);
          return;
        }
        ObjectIdentifier* empty_node_id;
        status = SynchronousGetEmptyNodeIdentifier(handler, &empty_node_id);
        if (status != Status::OK) {
          callback(status, false);
          return;
        }
        callback(Status::OK, commit->GetRootIdentifier() == *empty_node_id);
      });
}

void PageStorageImpl::GetUnsyncedCommits(
    fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](CoroutineHandler* handler,
             fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) {
        std::vector<std::unique_ptr<const Commit>> unsynced_commits;
        Status s = SynchronousGetUnsyncedCommits(handler, &unsynced_commits);
        callback(s, std::move(unsynced_commits));
      });
}

void PageStorageImpl::MarkCommitSynced(const CommitId& commit_id,
                                       fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, commit_id](CoroutineHandler* handler, fit::function<void(Status)> callback) {
        std::unique_ptr<const Commit> commit;
        Status status = SynchronousGetCommit(handler, commit_id, &commit);
        if (status != Status::OK) {
          callback(status);
          return;
        }
        callback(SynchronousMarkCommitSynced(handler, *commit));
      });
}

void PageStorageImpl::GetUnsyncedPieces(
    fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](CoroutineHandler* handler,
             fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
        std::vector<ObjectIdentifier> unsynced_object_identifiers;
        Status s = db_->GetUnsyncedPieces(handler, &unsynced_object_identifiers);
        callback(s, unsynced_object_identifiers);
      });
}

void PageStorageImpl::MarkPieceSynced(ObjectIdentifier object_identifier,
                                      fit::function<void(Status)> callback) {
  LEDGER_DCHECK(IsTokenValid(object_identifier));
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this, object_identifier = std::move(object_identifier)](
                               CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(db_->SetObjectStatus(handler, object_identifier, PageDbObjectStatus::SYNCED));
      });
}

void PageStorageImpl::IsPieceSynced(ObjectIdentifier object_identifier,
                                    fit::function<void(Status, bool)> callback) {
  LEDGER_DCHECK(IsTokenValid(object_identifier));
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler, fit::function<void(Status, bool)> callback) {
        PageDbObjectStatus object_status;
        Status status = db_->GetObjectStatus(handler, object_identifier, &object_status);
        callback(status, object_status == PageDbObjectStatus::SYNCED);
      });
}

void PageStorageImpl::MarkSyncedToPeer(fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      [this, callback = std::move(callback)](CoroutineHandler* handler) {
        std::unique_ptr<PageDb::Batch> batch;
        Status status = db_->StartBatch(handler, &batch);
        if (status != Status::OK) {
          callback(status);
          return;
        }
        status = SynchronousMarkPageOnline(handler, batch.get());
        if (status != Status::OK) {
          callback(status);
          return;
        }
        callback(batch->Execute(handler));
      });
}

void PageStorageImpl::AddObjectFromLocal(ObjectType object_type,
                                         std::unique_ptr<DataSource> data_source,
                                         ObjectReferencesAndPriority tree_references,
                                         fit::function<void(Status, ObjectIdentifier)> callback) {
  // |data_source| is not splitted yet: |tree_references| must contain only
  // BTree-level references, not piece-level references, and only in the case
  // where |data_source| actually represents a tree node.
  LEDGER_DCHECK(object_type == ObjectType::TREE_NODE || tree_references.empty());
  auto traced_callback = TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_add_object");

  auto managed_data_source = managed_container_.Manage(std::move(data_source));
  auto managed_data_source_ptr = managed_data_source->get();
  auto waiter = ledger::MakeRefCounted<ledger::StatusWaiter<Status>>(Status::OK);
  encryption_service_->GetChunkingPermutation(
      [this, waiter, managed_data_source = std::move(managed_data_source),
       tree_references = std::move(tree_references), managed_data_source_ptr, object_type,
       callback = std::move(traced_callback)](
          encryption::Status status,
          fit::function<uint64_t(uint64_t)> chunking_permutation) mutable {
        if (status != encryption::Status::OK) {
          callback(Status::INTERNAL_ERROR, ObjectIdentifier());
          return;
        }
        // Container to hold intermediate split pieces alive until the root piece has been written.
        auto live_pieces = std::make_unique<std::vector<ObjectIdentifier>>();
        SplitDataSource(
            managed_data_source_ptr, object_type,
            [this](ObjectDigest object_digest) {
              LEDGER_DCHECK(IsDigestValid(object_digest));
              return encryption_service_->MakeObjectIdentifier(&object_identifier_factory_,
                                                               std::move(object_digest));
            },
            std::move(chunking_permutation),
            [this, waiter, managed_data_source = std::move(managed_data_source),
             tree_references = std::move(tree_references), live_pieces = std::move(live_pieces),
             callback = std::move(callback)](IterationStatus status,
                                             std::unique_ptr<Piece> piece) mutable {
              if (status == IterationStatus::ERROR) {
                callback(Status::IO_ERROR, ObjectIdentifier());
                return;
              }

              LEDGER_DCHECK(piece != nullptr);
              ObjectIdentifier identifier = piece->GetIdentifier();
              auto object_info = GetObjectDigestInfo(identifier.object_digest());
              if (!object_info.is_inlined()) {
                ObjectReferencesAndPriority piece_references;
                if (piece->AppendReferences(&piece_references) != Status::OK) {
                  // The piece is generated internally by splitting, not coming from
                  // untrusted source, so decoding should never fail.
                  callback(Status::INTERNAL_ERROR, ObjectIdentifier());
                  return;
                }
                if (object_info.object_type == ObjectType::TREE_NODE) {
                  // There is at most one TREE_NODE, and it must be the last piece, so
                  // it is safe to add tree_references to piece_references there.
                  LEDGER_DCHECK(status == IterationStatus::DONE);
                  piece_references.insert(std::make_move_iterator(tree_references.begin()),
                                          std::make_move_iterator(tree_references.end()));
                }
                // Keep the piece alive through the shared container before yielding it to AddPiece.
                live_pieces->push_back(piece->GetIdentifier());
                AddPiece(std::move(piece), ChangeSource::LOCAL, IsObjectSynced::NO,
                         std::move(piece_references), waiter->NewCallback());
              }
              if (status == IterationStatus::IN_PROGRESS)
                return;

              LEDGER_DCHECK(status == IterationStatus::DONE);
              waiter->Finalize([identifier = std::move(identifier),
                                live_pieces = std::move(live_pieces),
                                callback = std::move(callback)](Status status) mutable {
                callback(status, std::move(identifier));
                // At this point, all pieces have been written and we can release |live_pieces|
                // safely.
              });
            });
      });
}

Status PageStorageImpl::DeleteObject(coroutine::CoroutineHandler* handler,
                                     ObjectDigest object_digest,
                                     ObjectReferencesAndPriority* references) {
  if (GetObjectDigestInfo(object_digest).is_inlined()) {
    LEDGER_VLOG(2) << "Object is inline, cannot be deleted: " << object_digest;
    return Status::INTERNAL_NOT_FOUND;
  }
  if (object_identifier_factory_.count(object_digest) != 0) {
    // A reference to this object has been created since garbage collection has been scheduled.
    return Status::CANCELED;
  }
  if (!pending_garbage_collection_.insert(object_digest).second) {
    // Delete object is already in progress.
    return Status::CANCELED;
  }
  auto cleanup_pending =
      fit::defer(callback::MakeScoped(weak_factory_.GetWeakPtr(), [this, object_digest] {
        pending_garbage_collection_.erase(object_digest);
      }));
  if (environment_->gc_policy() == GarbageCollectionPolicy::EAGER_LIVE_REFERENCES) {
    // With the eager GC policy we expect most GC attempts to be unsuccessful.  Failed GC attempts
    // are costly: we need to load the references, but end up not using them. This also creates a
    // lot of short-lived object identifiers that will themselves trigger a failed collection (since
    // they are referenced by the object we just failed to delete).
    // Avoid this by checking that the object is deletable before doing anything.
    std::vector<std::string> object_status_keys;
    RETURN_ON_ERROR(db_->EnsureObjectDeletable(handler, object_digest, &object_status_keys));
  }
  // Collect outbound references from the deleted object. Scope ancillary variables to avoid
  // live references to the object when calling |PageDb::DeleteObject| below, which would
  // abort the deletion.
  references->clear();
  {
    std::unique_ptr<const Piece> piece;
    // This object identifier is used only to read piece data from storage. The key index can
    // be arbitrary, it is ignored.
    ObjectIdentifier identifier =
        object_identifier_factory_.MakeObjectIdentifier(0u, object_digest);
    RETURN_ON_ERROR(db_->ReadObject(handler, identifier, &piece));
    RETURN_ON_ERROR(piece->AppendReferences(references));
    // Read tree references if necessary.
    if (GetObjectDigestInfo(object_digest).object_type == ObjectType::TREE_NODE) {
      Status status;
      std::unique_ptr<const Object> object;
      if (coroutine::SyncCall(
              handler,
              [this, identifier](
                  fit::function<void(Status, std::unique_ptr<const Object>)> callback) mutable {
                GetObject(identifier, Location::Local(), std::move(callback));
              },
              &status, &object) == coroutine::ContinuationStatus::INTERRUPTED) {
        return Status::INTERRUPTED;
      }
      if (status != Status::OK) {
        return status;
      }
      RETURN_ON_ERROR(object->AppendReferences(references));
    }
  }
  return db_->DeleteObject(handler, object_digest, *references);
}

void PageStorageImpl::GetObjectPart(ObjectIdentifier object_identifier, int64_t offset,
                                    int64_t max_size, Location location,
                                    fit::function<void(Status, ledger::SizedVmo)> callback) {
  LEDGER_DCHECK(IsDigestValid(object_identifier.object_digest()));
  LEDGER_DCHECK(GetObjectDigestInfo(object_identifier.object_digest()).object_type ==
                ObjectType::BLOB);
  LEDGER_DCHECK(IsTokenValid(object_identifier));
  GetOrDownloadPiece(
      object_identifier, location,
      [this, location, object_identifier = std::move(object_identifier), offset, max_size,
       callback = std::move(callback)](Status status, std::unique_ptr<const Piece> piece,
                                       WritePieceCallback write_callback) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        LEDGER_DCHECK(piece);
        // |piece| is necessarily a blob, so it must have been retrieved from
        // disk or written to disk already.
        LEDGER_DCHECK(!write_callback);

        // If we are reading zero bytes, bail out now.
        if (max_size == 0) {
          ledger::SizedVmo buffer;
          if (!ledger::VmoFromString("", &buffer)) {
            callback(Status::INTERNAL_ERROR, nullptr);
            return;
          }
          callback(Status::OK, std::move(buffer));
          return;
        }

        ObjectDigestInfo digest_info = GetObjectDigestInfo(piece->GetIdentifier().object_digest());

        // If the piece is a chunk, then the piece represents the whole object.
        if (digest_info.is_chunk()) {
          const absl::string_view data = piece->GetData();
          ledger::SizedVmo buffer;
          int64_t start = GetObjectPartStart(offset, data.size());
          int64_t length = GetObjectPartLength(max_size, data.size(), start);
          if (!ledger::VmoFromString(data.substr(start, length), &buffer)) {
            callback(Status::INTERNAL_ERROR, nullptr);
            return;
          }
          callback(Status::OK, std::move(buffer));
          return;
        }

        LEDGER_DCHECK(digest_info.piece_type == PieceType::INDEX);
        // We do not need to keep children pieces alive with in-memory references because we have
        // already the root piece to disk, creating on-disk references.
        GetIndexObject(*piece, offset, max_size, location, /*child_identifiers=*/nullptr,
                       std::move(callback));
      });
}

void PageStorageImpl::GetObject(
    ObjectIdentifier object_identifier, Location location,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  fit::function<void(Status, std::unique_ptr<const Object>)> traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_get_object");
  LEDGER_DCHECK(IsDigestValid(object_identifier.object_digest()));
  LEDGER_DCHECK(IsTokenValid(object_identifier));

  GetOrDownloadPiece(
      object_identifier, location,
      [this, location, object_identifier, callback = std::move(traced_callback)](
          Status status, std::unique_ptr<const Piece> piece,
          WritePieceCallback write_callback) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        LEDGER_DCHECK(piece);
        ObjectDigestInfo digest_info = GetObjectDigestInfo(piece->GetIdentifier().object_digest());

        // If the piece is a chunk, then the piece represents the whole object.
        if (digest_info.is_chunk()) {
          LEDGER_DCHECK(!write_callback);
          callback(Status::OK, std::make_unique<ChunkObject>(std::move(piece)));
          return;
        }

        LEDGER_DCHECK(digest_info.piece_type == PieceType::INDEX);
        // A container which will be filled with the identifiers of the children of |piece|, to keep
        // them alive until write_callback has completed, ie. until |piece| has been written to
        // disk with its references and |callback| is called.
        std::vector<ObjectIdentifier>* child_identifiers = nullptr;
        if (write_callback) {
          auto keep_alive = std::make_unique<std::vector<ObjectIdentifier>>();
          child_identifiers = keep_alive.get();
          callback = [keep_alive = std::move(keep_alive), callback = std::move(callback)](
                         Status status, std::unique_ptr<const Object> object) {
            callback(status, std::move(object));
          };
        }
        // This reference remains valid as long as |piece| is valid. The latter
        // is owned by the final callback passed to GetIndexObject, so it
        // outlives the former.
        const Piece& piece_ref = *piece;
        GetIndexObject(piece_ref, 0, -1, location, child_identifiers,
                       [piece = std::move(piece), object_identifier,
                        write_callback = std::move(write_callback), callback = std::move(callback)](
                           Status status, ledger::SizedVmo vmo) mutable {
                         if (status != Status::OK) {
                           callback(status, nullptr);
                           return;
                         }
                         auto object = std::make_unique<VmoObject>(std::move(object_identifier),
                                                                   std::move(vmo));
                         if (write_callback) {
                           write_callback(std::move(piece), std::move(object), std::move(callback));
                         } else {
                           callback(status, std::move(object));
                         }
                       });
      });
}

void PageStorageImpl::GetPiece(ObjectIdentifier object_identifier,
                               fit::function<void(Status, std::unique_ptr<const Piece>)> callback) {
  LEDGER_DCHECK(IsTokenValid(object_identifier));
  ObjectDigestInfo digest_info = GetObjectDigestInfo(object_identifier.object_digest());
  if (digest_info.is_inlined()) {
    callback(Status::OK, std::make_unique<InlinePiece>(std::move(object_identifier)));
    return;
  }

  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<const Piece>)> callback) mutable {
        std::unique_ptr<const Piece> piece;
        Status status = db_->ReadObject(handler, std::move(object_identifier), &piece);
        callback(status, std::move(piece));
      });
}

void PageStorageImpl::SetSyncMetadata(absl::string_view key, absl::string_view value,
                                      fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this, key = convert::ToString(key), value = convert::ToString(value)](
                               CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(db_->SetSyncMetadata(handler, key, value));
      });
}

void PageStorageImpl::GetSyncMetadata(absl::string_view key,
                                      fit::function<void(Status, std::string)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, key = convert::ToString(key)](CoroutineHandler* handler,
                                           fit::function<void(Status, std::string)> callback) {
        std::string value;
        Status status = db_->GetSyncMetadata(handler, key, &value);
        callback(status, std::move(value));
      });
}

void PageStorageImpl::GetCommitContents(const Commit& commit, std::string min_key,
                                        fit::function<bool(Entry)> on_next,
                                        fit::function<void(Status)> on_done) {
  btree::ForEachEntry(
      environment_->coroutine_service(), this,
      {commit.GetRootIdentifier(), PageStorage::Location::TreeNodeFromNetwork(commit.GetId())},
      min_key, std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetEntryFromCommit(const Commit& commit, std::string key,
                                         fit::function<void(Status, Entry)> callback) {
  std::unique_ptr<bool> key_found = std::make_unique<bool>(false);
  auto on_next = [key, key_found = key_found.get(), callback = callback.share()](Entry next) {
    if (next.key == key) {
      *key_found = true;
      callback(Status::OK, next);
    }
    return false;
  };

  auto on_done = [key_found = std::move(key_found), callback = std::move(callback)](Status s) {
    if (*key_found) {
      return;
    }
    if (s == Status::OK) {
      callback(Status::KEY_NOT_FOUND, Entry());
      return;
    }
    callback(s, Entry());
  };
  btree::ForEachEntry(
      environment_->coroutine_service(), this,
      {commit.GetRootIdentifier(), PageStorage::Location::TreeNodeFromNetwork(commit.GetId())},
      std::move(key), std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetDiffForCloud(
    const Commit& target_commit,
    fit::function<void(Status, CommitIdView, std::vector<EntryChange>)> callback) {
  // Use the first parent as the base commit.
  const CommitId base_id = convert::ToString(target_commit.GetParentIds()[0]);
  GetCommit(base_id,
            callback::MakeScoped(
                weak_factory_.GetWeakPtr(),
                [this, target_commit = target_commit.Clone(), callback = std::move(callback)](
                    Status status, std::unique_ptr<const Commit> base_commit) mutable {
                  // TODO(nellyv): Here we assume that the parent commit is available: when we start
                  // prunning synced commits it might not be the case and another commit should be
                  // used instead.
                  LEDGER_DCHECK(status != Status::INTERNAL_NOT_FOUND);
                  if (status != Status::OK) {
                    callback(status, "", {});
                    return;
                  }
                  auto changes = std::make_unique<std::vector<EntryChange>>();
                  auto on_next_diff = [weak_this = weak_factory_.GetWeakPtr(),
                                       changes = changes.get()](TwoWayChange change) {
                    if (!weak_this) {
                      return false;
                    }
                    if (change.base) {
                      LEDGER_DCHECK(!change.base->entry_id.empty());
                      // This change is either an update or a deletion. In either case we send to
                      // the cloud a deletion of the previous entry.
                      changes->push_back({std::move(*change.base), /*deleted*/ true});
                    }
                    if (change.target) {
                      LEDGER_DCHECK(!change.target->entry_id.empty());
                      // This change is either an update or an insertion. In either case we send to
                      // the cloud an insertion of the updated entry.
                      changes->push_back({std::move(*change.target), /*deleted*/ false});
                    }
                    return true;
                  };
                  auto on_done = [base_id = base_commit->GetId(), changes = std::move(changes),
                                  callback = std::move(callback)](Status status) {
                    if (status != Status::OK) {
                      callback(status, "", {});
                    }
                    callback(status, base_id, std::move(*changes));
                  };

                  // We expect both commits to be present locally.
                  btree::ForEachTwoWayDiff(
                      environment_->coroutine_service(), this,
                      {base_commit->GetRootIdentifier(), PageStorage::Location::Local()},
                      {target_commit->GetRootIdentifier(), PageStorage::Location::Local()}, "",
                      std::move(on_next_diff), std::move(on_done));
                }));
}

void PageStorageImpl::GetCommitContentsDiff(const Commit& base_commit, const Commit& other_commit,
                                            std::string min_key,
                                            fit::function<bool(EntryChange)> on_next_diff,
                                            fit::function<void(Status)> on_done) {
  btree::ForEachDiff(environment_->coroutine_service(), this,
                     {base_commit.GetRootIdentifier(),
                      PageStorage::Location::TreeNodeFromNetwork(base_commit.GetId())},
                     {other_commit.GetRootIdentifier(),
                      PageStorage::Location::TreeNodeFromNetwork(other_commit.GetId())},
                     std::move(min_key), std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::GetThreeWayContentsDiff(const Commit& base_commit, const Commit& left_commit,
                                              const Commit& right_commit, std::string min_key,
                                              fit::function<bool(ThreeWayChange)> on_next_diff,
                                              fit::function<void(Status)> on_done) {
  btree::ForEachThreeWayDiff(environment_->coroutine_service(), this,
                             {base_commit.GetRootIdentifier(),
                              PageStorage::Location::TreeNodeFromNetwork(base_commit.GetId())},
                             {left_commit.GetRootIdentifier(),
                              PageStorage::Location::TreeNodeFromNetwork(left_commit.GetId())},
                             {right_commit.GetRootIdentifier(),
                              PageStorage::Location::TreeNodeFromNetwork(right_commit.GetId())},
                             std::move(min_key), std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::GetClock(fit::function<void(Status, Clock)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](coroutine::CoroutineHandler* handler, fit::function<void(Status, Clock)> callback) {
        Clock clock;
        Status status = db_->GetClock(handler, &clock);
        callback(status, std::move(clock));
      });
}

void PageStorageImpl::GetCommitIdFromRemoteId(absl::string_view remote_commit_id,
                                              fit::function<void(Status, CommitId)> callback) {
  auto it = remote_ids_of_commits_being_added_.find(remote_commit_id);
  if (it != remote_ids_of_commits_being_added_.end()) {
    callback(Status::OK, it->second);
    return;
  }
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, remote_commit_id = convert::ToString(remote_commit_id)](
          coroutine::CoroutineHandler* handler, fit::function<void(Status, CommitId)> callback) {
        CommitId commit_id;
        Status status = db_->GetCommitIdFromRemoteId(handler, remote_commit_id, &commit_id);
        callback(status, std::move(commit_id));
      });
}

Status PageStorageImpl::DeleteCommits(coroutine::CoroutineHandler* handler,
                                      std::vector<std::unique_ptr<const Commit>> commits) {
  std::unique_ptr<PageDb::Batch> batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  for (const std::unique_ptr<const Commit>& commit : commits) {
    auto parents = commit->GetParentIds();
    if (parents.size() > 1) {
      RETURN_ON_ERROR(batch->DeleteMerge(handler, parents[0], parents[1], commit->GetId()));
    }
    RETURN_ON_ERROR(batch->DeleteCommit(handler, commit->GetId(),
                                        encryption_service_->EncodeCommitId(commit->GetId()),
                                        commit->GetRootIdentifier()));
  }
  RETURN_ON_ERROR(batch->Execute(handler));
  for (const std::unique_ptr<const Commit>& commit : commits) {
    commit_factory_.RemoveCommitDependencies(commit->GetId());
    object_identifier_factory_.NotifyOnUntracked(commit->GetRootIdentifier().object_digest());
  }
  return Status::OK;
}

Status PageStorageImpl::SetClock(coroutine::CoroutineHandler* handler, const Clock& clock) {
  RETURN_ON_ERROR(db_->SetClock(handler, clock));
  if (page_sync_) {
    page_sync_->UpdateClock(clock, [](ledger::Status /*status*/) {
      // We don't care whether the clock propagated correctly here. We care only when we want to
      // ensure we got all clock updates before performing garbage collection.
    });
  }
  return Status::OK;
}

void PageStorageImpl::NotifyWatchersOfNewCommits(
    const std::vector<std::unique_ptr<const Commit>>& new_commits, ChangeSource source) {
  for (auto& watcher : watchers_) {
    watcher.OnNewCommits(new_commits, source);
  }
}

void PageStorageImpl::GetCommitRootIdentifier(
    CommitIdView commit_id, fit::function<void(Status, ObjectIdentifier)> callback) {
  if (auto it = roots_of_commits_being_added_.find(commit_id);
      it != roots_of_commits_being_added_.end()) {
    callback(Status::OK, it->second);
  } else {
    GetCommit(commit_id, [callback = std::move(callback)](Status status,
                                                          std::unique_ptr<const Commit> commit) {
      if (status != Status::OK) {
        callback(status, {});
        return;
      }
      LEDGER_DCHECK(commit);
      callback(Status::OK, commit->GetRootIdentifier());
    });
  }
}

void PageStorageImpl::ScheduleObjectGarbageCollection(const ObjectDigest& object_digest) {
  coroutine_manager_.StartCoroutine(
      [this, object_digest = object_digest](CoroutineHandler* handler) mutable {
        std::queue<ObjectDigest> to_delete;
        to_delete.push(object_digest);
        while (!to_delete.empty()) {
          object_digest = to_delete.front();
          to_delete.pop();
          ObjectReferencesAndPriority references;
          Status status = DeleteObject(handler, std::move(object_digest), &references);
          if (status == Status::INTERRUPTED) {
            return;
          }
          if (status == Status::OK) {
            for (const auto& [object_digest, priority] : references) {
              to_delete.push(object_digest);
            }
          }
        }
      });
}

Status PageStorageImpl::MarkAllPiecesLocal(CoroutineHandler* handler, PageDb::Batch* batch,
                                           std::vector<ObjectIdentifier> object_identifiers) {
  std::set<ObjectIdentifier> seen_identifiers;
  while (!object_identifiers.empty()) {
    auto it = seen_identifiers.insert(std::move(object_identifiers.back()));
    object_identifiers.pop_back();
    const ObjectIdentifier& object_identifier = *(it.first);
    LEDGER_DCHECK(!GetObjectDigestInfo(object_identifier.object_digest()).is_inlined());
    LEDGER_DCHECK(IsTokenValid(object_identifier));
    RETURN_ON_ERROR(batch->SetObjectStatus(handler, object_identifier, PageDbObjectStatus::LOCAL));
    if (GetObjectDigestInfo(object_identifier.object_digest()).piece_type == PieceType::INDEX) {
      std::unique_ptr<const Piece> piece;
      RETURN_ON_ERROR(db_->ReadObject(handler, object_identifier, &piece));
      absl::string_view content = piece->GetData();

      const FileIndex* file_index;
      RETURN_ON_ERROR(FileIndexSerialization::ParseFileIndex(content, &file_index));

      object_identifiers.reserve(object_identifiers.size() + file_index->children()->size());
      for (const auto* child : *file_index->children()) {
        ObjectIdentifier new_object_identifier =
            ToObjectIdentifier(child->object_identifier(), &object_identifier_factory_);
        if (!GetObjectDigestInfo(new_object_identifier.object_digest()).is_inlined()) {
          if (!seen_identifiers.count(new_object_identifier)) {
            object_identifiers.push_back(std::move(new_object_identifier));
          }
        }
      }
    }
  }
  return Status::OK;
}

Status PageStorageImpl::ContainsCommit(CoroutineHandler* handler, CommitIdView id) {
  if (IsFirstCommit(id)) {
    return Status::OK;
  }
  std::string bytes;
  return db_->GetCommitStorageBytes(handler, id, &bytes);
}

bool PageStorageImpl::IsFirstCommit(CommitIdView id) { return id == kFirstPageCommitId; }

void PageStorageImpl::AddPiece(std::unique_ptr<const Piece> piece, ChangeSource source,
                               IsObjectSynced is_object_synced,
                               ObjectReferencesAndPriority references,
                               fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, piece = std::move(piece), source, references = std::move(references),
       is_object_synced](CoroutineHandler* handler, fit::function<void(Status)> callback) mutable {
        callback(
            SynchronousAddPiece(handler, *piece, source, is_object_synced, std::move(references)));
      });
}

void PageStorageImpl::ObjectIsUntracked(ObjectIdentifier object_identifier,
                                        fit::function<void(Status, bool)> callback) {
  LEDGER_DCHECK(IsTokenValid(object_identifier));
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler, fit::function<void(Status, bool)> callback) mutable {
        if (GetObjectDigestInfo(object_identifier.object_digest()).is_inlined()) {
          callback(Status::OK, false);
          return;
        }

        PageDbObjectStatus object_status;
        Status status = db_->GetObjectStatus(handler, object_identifier, &object_status);
        callback(status, object_status == PageDbObjectStatus::TRANSIENT);
      });
}

std::string PageStorageImpl::GetEntryId() { return encryption_service_->GetEntryId(); }

std::string PageStorageImpl::GetEntryIdForMerge(absl::string_view entry_name,
                                                CommitIdView left_parent_id,
                                                CommitIdView right_parent_id,
                                                absl::string_view operation_list) {
  return encryption_service_->GetEntryIdForMerge(entry_name, convert::ToString(left_parent_id),
                                                 convert::ToString(right_parent_id),
                                                 operation_list);
}

void PageStorageImpl::GetIndexObject(const Piece& piece, int64_t offset, int64_t max_size,
                                     Location location,
                                     std::vector<ObjectIdentifier>* child_identifiers,
                                     fit::function<void(Status, ledger::SizedVmo)> callback) {
  ObjectDigestInfo digest_info = GetObjectDigestInfo(piece.GetIdentifier().object_digest());

  LEDGER_DCHECK(digest_info.piece_type == PieceType::INDEX);
  absl::string_view content = piece.GetData();
  const FileIndex* file_index;
  Status status = FileIndexSerialization::ParseFileIndex(content, &file_index);
  if (status != Status::OK) {
    callback(Status::DATA_INTEGRITY_ERROR, nullptr);
    return;
  }

  int64_t start = GetObjectPartStart(offset, file_index->size());
  int64_t length = GetObjectPartLength(max_size, file_index->size(), start);
  zx::vmo raw_vmo;
  zx_status_t zx_status = zx::vmo::create(length, 0, &raw_vmo);
  if (zx_status != ZX_OK) {
    LEDGER_LOG(WARNING) << "Unable to create VMO of size " << length << ": "
                        << zx_status_get_string(zx_status);
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }
  ledger::SizedVmo vmo(std::move(raw_vmo), length);

  ledger::SizedVmo vmo_copy;
  zx_status = vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
  if (zx_status != ZX_OK) {
    LEDGER_LOG(ERROR) << "Unable to duplicate vmo: " << zx_status_get_string(zx_status);
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }

  // Keep the children of the index object alive before getting them recursively in
  // FillBufferWithObjectContent.
  if (child_identifiers) {
    for (const auto* child : *file_index->children()) {
      child_identifiers->push_back(
          ToObjectIdentifier(child->object_identifier(), &object_identifier_factory_));
    }
  }

  FillBufferWithObjectContent(
      piece, std::move(vmo_copy), start, length, 0, file_index->size(), location,
      [vmo = std::move(vmo), callback = std::move(callback)](Status status) mutable {
        callback(status, std::move(vmo));
      });
}

void PageStorageImpl::FillBufferWithObjectContent(const Piece& piece, ledger::SizedVmo vmo,
                                                  int64_t global_offset, int64_t global_size,
                                                  int64_t current_position, int64_t object_size,
                                                  Location location,
                                                  fit::function<void(Status)> callback) {
  absl::string_view content = piece.GetData();
  ObjectDigestInfo digest_info = GetObjectDigestInfo(piece.GetIdentifier().object_digest());
  if (digest_info.is_inlined() || digest_info.is_chunk()) {
    if (object_size != static_cast<int64_t>(content.size())) {
      LEDGER_LOG(ERROR) << "Error in serialization format. Expecting object: "
                        << piece.GetIdentifier() << " to have size: " << object_size
                        << ", but found an object of size: " << content.size();
      callback(Status::DATA_INTEGRITY_ERROR);
      return;
    }
    // Distance is negative if the offset is ahead and positive if behind.
    int64_t distance_from_global_offset = current_position - global_offset;
    // Read offset can be non-zero on first read; in that case, we need to skip
    // bytes coming before global offset.
    int64_t read_offset = std::max(-distance_from_global_offset, 0L);
    // Write offset is zero on the first write; otherwise we need to skip number
    // of bytes corresponding to what we have already written.
    int64_t write_offset = std::max(distance_from_global_offset, 0L);
    // Read and write until reaching either size of the object, or global size.
    int64_t read_write_size =
        std::min(static_cast<int64_t>(content.size()) - read_offset, global_size - write_offset);
    LEDGER_DCHECK(read_write_size > 0);
    absl::string_view read_substr = content.substr(read_offset, read_write_size);
    zx_status_t zx_status = vmo.vmo().write(read_substr.data(), write_offset, read_write_size);
    if (zx_status != ZX_OK) {
      LEDGER_LOG(ERROR) << "Unable to write to vmo: " << zx_status_get_string(zx_status);
      callback(Status::INTERNAL_ERROR);
      return;
    }
    callback(Status::OK);
    return;
  }

  const FileIndex* file_index;
  Status status = FileIndexSerialization::ParseFileIndex(content, &file_index);
  if (status != Status::OK) {
    callback(Status::DATA_INTEGRITY_ERROR);
    return;
  }
  if (static_cast<int64_t>(file_index->size()) != object_size) {
    LEDGER_LOG(ERROR) << "Error in serialization format. Expecting object: "
                      << piece.GetIdentifier() << " to have size " << object_size
                      << ", but found an index object of size: " << file_index->size();
    callback(Status::DATA_INTEGRITY_ERROR);
    return;
  }

  // Iterate over the children pieces, recursing into the ones corresponding to
  // the part of the object to be copied to the VMO.
  int64_t sub_offset = 0;
  auto waiter = ledger::MakeRefCounted<ledger::StatusWaiter<Status>>(Status::OK);
  for (const auto* child : *file_index->children()) {
    if (sub_offset + child->size() > file_index->size()) {
      callback(Status::DATA_INTEGRITY_ERROR);
      return;
    }
    int64_t child_position = current_position + sub_offset;
    ObjectIdentifier child_identifier =
        ToObjectIdentifier(child->object_identifier(), &object_identifier_factory_);
    // Skip children before the part to copy.
    if (child_position + static_cast<int64_t>(child->size()) <= global_offset) {
      sub_offset += child->size();
      continue;
    }
    // Stop iterating as soon as the part has been fully copied.
    if (global_offset + global_size <= child_position) {
      break;
    }
    // Create a copy of the VMO to be owned by the recursive call.
    ledger::SizedVmo vmo_copy;
    zx_status_t zx_status = vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
    if (zx_status != ZX_OK) {
      LEDGER_LOG(ERROR) << "Unable to duplicate vmo: " << zx_status_get_string(zx_status);
      callback(Status::INTERNAL_ERROR);
      return;
    }
    // This is a child, so it cannot be a tree node, only top pieces may be tree
    // nodes.
    LEDGER_DCHECK(GetObjectDigestInfo(child_identifier.object_digest()).object_type ==
                  ObjectType::BLOB);
    GetOrDownloadPiece(
        child_identifier, location,
        [this, vmo = std::move(vmo_copy), global_offset, global_size, child_position,
         child_size = child->size(), location, child_callback = waiter->NewCallback()](
            Status status, std::unique_ptr<const Piece> child_piece,
            WritePieceCallback write_callback) mutable {
          if (status != Status::OK) {
            child_callback(status);
            return;
          }
          LEDGER_DCHECK(child_piece);
          LEDGER_DCHECK(!write_callback);
          // The |child_piece| is necessarily a blob, so it must have been read from
          // or written to disk already. As such, its children will be kept alive by
          // on-disk references when we get them recursively.
          FillBufferWithObjectContent(
              *child_piece, std::move(vmo), global_offset, global_size, child_position, child_size,
              location, [child_callback = std::move(child_callback)](Status status) mutable {
                child_callback(status);
              });
        });
    sub_offset += child->size();
  }
  waiter->Finalize(std::move(callback));
}

void PageStorageImpl::GetOrDownloadPiece(
    ObjectIdentifier object_identifier, Location location,
    fit::function<void(Status, std::unique_ptr<const Piece>, WritePieceCallback)> callback) {
  GetPiece(object_identifier, [this, callback = std::move(callback), location,
                               object_identifier = std::move(object_identifier)](
                                  Status status, std::unique_ptr<const Piece> piece) mutable {
    // Object was found.
    if (status == Status::OK) {
      callback(status, std::move(piece), {});
      return;
    }
    LEDGER_DCHECK(piece == nullptr);
    // An unexpected error occured.
    if (status != Status::INTERNAL_NOT_FOUND || location.is_local()) {
      callback(status, nullptr, {});
      return;
    }
    // Object not found locally, attempt to download it.
    LEDGER_DCHECK(location.is_network());
    if (!page_sync_) {
      callback(Status::NETWORK_ERROR, nullptr, {});
      return;
    }
    auto waiter = ledger::MakeRefCounted<
        ledger::AnyWaiter<Status, std::tuple<std::unique_ptr<const Piece>, WritePieceCallback>>>(
        Status::OK, Status::INTERNAL_NOT_FOUND,
        std::tuple<std::unique_ptr<const Piece>, WritePieceCallback>());

    // If we are looking for a piece of a tree node, try diffs.
    if (location.is_tree_node_from_network()) {
      auto download_diff_callback = [callback = waiter->NewCallback()](
                                        Status status, std::unique_ptr<const Piece> piece) {
        callback(status, std::make_tuple(std::move(piece), nullptr));
      };

      DownloadPieceUsingDiff(object_identifier, location.in_commit(),
                             std::move(download_diff_callback));
    }

    // And also with objects.
    auto download_directly_callback = [callback = waiter->NewCallback()](
                                          Status status, std::unique_ptr<const Piece> piece,
                                          WritePieceCallback write_callback) {
      callback(status, std::make_tuple(std::move(piece), std::move(write_callback)));
    };

    DownloadPieceDirectly(std::move(object_identifier), std::move(location),
                          std::move(download_directly_callback));

    waiter->Finalize(
        [callback = std::move(callback)](
            Status status, std::tuple<std::unique_ptr<const Piece>, WritePieceCallback> result) {
          callback(status, std::move(std::get<0>(result)), std::move(std::get<1>(result)));
        });
  });
}

void PageStorageImpl::DownloadPieceDirectly(
    ObjectIdentifier object_identifier, Location location,
    fit::function<void(Status, std::unique_ptr<const Piece>, WritePieceCallback)> callback) {
  {
    download_manager_.StartCoroutine(
        std::move(callback),
        [this, object_identifier = std::move(object_identifier), location = std::move(location)](
            CoroutineHandler* handler,
            fit::function<void(Status, std::unique_ptr<const Piece>, WritePieceCallback)>
                callback) mutable {
          Status status;
          ChangeSource source;
          IsObjectSynced is_object_synced;
          std::unique_ptr<DataSource::DataChunk> chunk;
          LEDGER_DCHECK(location.is_network());
          RetrievedObjectType retrieved_object_type = location.is_tree_node_from_network()
                                                          ? RetrievedObjectType::TREE_NODE
                                                          : RetrievedObjectType::BLOB;

          // Retrieve an object from the network.
          if (coroutine::SyncCall(
                  handler,
                  [this, object_identifier, retrieved_object_type](
                      fit::function<void(Status, ChangeSource, IsObjectSynced,
                                         std::unique_ptr<DataSource::DataChunk>)>
                          callback) mutable {
                    page_sync_->GetObject(std::move(object_identifier), retrieved_object_type,
                                          std::move(callback));
                  },
                  &status, &source, &is_object_synced,
                  &chunk) == coroutine::ContinuationStatus::INTERRUPTED) {
            callback(Status::INTERRUPTED, nullptr, {});
            return;
          }
          if (status != Status::OK) {
            callback(status, nullptr, {});
            return;
          }
          // Sanity-check of retrieved object.
          auto digest_info = GetObjectDigestInfo(object_identifier.object_digest());
          LEDGER_DCHECK(!digest_info.is_inlined());

          if (object_identifier.object_digest() !=
              ComputeObjectDigest(digest_info.piece_type, digest_info.object_type, chunk->Get())) {
            callback(Status::DATA_INTEGRITY_ERROR, nullptr, {});
            return;
          }
          std::unique_ptr<const Piece> piece =
              std::make_unique<DataChunkPiece>(std::move(object_identifier), std::move(chunk));

          // Write the piece to disk if possible. Index tree nodes cannot be
          // written at this stage as we need the full object.
          if (digest_info.object_type == ObjectType::TREE_NODE &&
              digest_info.piece_type == PieceType::INDEX) {
            // Return a WritePiece callback since the piece has not been written to disk.
            callback(
                Status::OK, std::move(piece),
                [this, source, is_object_synced](
                    std::unique_ptr<const Piece> piece, std::unique_ptr<const Object> object,
                    fit::function<void(Status, std::unique_ptr<const Object>)> final_callback) {
                  ObjectReferencesAndPriority references;
                  Status status = piece->AppendReferences(&references);
                  if (status != Status::OK) {
                    final_callback(status, nullptr);
                    return;
                  }
                  status = object->AppendReferences(&references);
                  if (status != Status::OK) {
                    final_callback(status, nullptr);
                    return;
                  }
                  // The piece is moved to |AddPiece| but is kept alive through the identifier
                  // embedded in |object| which is passed to |final_callback|.
                  AddPiece(std::move(piece), source, is_object_synced, references,
                           [final_callback = std::move(final_callback),
                            object = std::move(object)](Status status) mutable {
                             if (status != Status::OK) {
                               final_callback(status, nullptr);
                               return;
                             }
                             final_callback(Status::OK, std::move(object));
                           });
                });
            return;
          }
          ObjectReferencesAndPriority references;
          status = piece->AppendReferences(&references);
          if (status != Status::OK) {
            callback(status, nullptr, {});
            return;
          }
          if (digest_info.object_type == ObjectType::TREE_NODE) {
            LEDGER_DCHECK(digest_info.is_chunk());
            // Convert the piece to a chunk Object to extract its
            // references.
            auto object = std::make_unique<ChunkObject>(std::move(piece));
            status = object->AppendReferences(&references);
            if (status != Status::OK) {
              callback(status, nullptr, {});
              return;
            }
            piece = object->ReleasePiece();
          }
          status = SynchronousAddPiece(handler, *piece, source, is_object_synced, references);
          if (status != Status::OK) {
            callback(status, nullptr, {});
            return;
          }
          callback(Status::OK, std::move(piece), {});
        });
  }
}

void PageStorageImpl::DownloadPieceUsingDiff(
    ObjectIdentifier object_identifier, CommitId containing_commit,
    fit::function<void(Status, std::unique_ptr<const Piece>)> callback) {
  fit::function<void(Status)> on_downloaded =
      [this, object_identifier = std::move(object_identifier),
       callback = std::move(callback)](Status status) mutable {
        if (status != Status::OK) {
          callback(status, {});
          return;
        }

        // The object is now present locally.
        GetPiece(std::move(object_identifier), std::move(callback));
      };

  coroutine_manager_.StartCoroutine(
      std::move(on_downloaded),
      [this, containing_commit = std::move(containing_commit)](
          CoroutineHandler* handler, fit::function<void(Status)> callback) mutable {
        callback(SynchronousDownloadDiff(handler, std::move(containing_commit)));
      });
}

Status PageStorageImpl::SynchronousDownloadDiff(coroutine::CoroutineHandler* handler,
                                                CommitId target_commit_id) {
  Status status;
  std::vector<CommitId> bases;
  if (coroutine::SyncCall(
          handler,
          [this, &target_commit_id](fit::callback<void(Status, std::vector<CommitId>)> callback) {
            ChooseDiffBases(target_commit_id, std::move(callback));
          },
          &status, &bases) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (status != Status::OK) {
    return status;
  }

  CommitId base_commit_id;
  std::vector<EntryChange> changes;
  if (coroutine::SyncCall(
          handler,
          [this, target_commit_id, bases = std::move(bases)](
              fit::function<void(Status, CommitId, std::vector<EntryChange>)> callback) mutable {
            page_sync_->GetDiff(std::move(target_commit_id), std::move(bases), std::move(callback));
          },
          &status, &base_commit_id, &changes) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (status != Status::OK) {
    return status;
  }

  // The base commit might be one of the commits we are currently downloading. It is fine to use it
  // directly, as we just need to know what is its root identifier.
  auto waiter = ledger::MakeRefCounted<ledger::Waiter<Status, ObjectIdentifier>>(Status::OK);
  GetCommitRootIdentifier(base_commit_id, waiter->NewCallback());
  GetCommitRootIdentifier(target_commit_id, waiter->NewCallback());

  std::vector<ObjectIdentifier> commit_root_ids;
  if (coroutine::Wait(handler, waiter, &status, &commit_root_ids) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (status != Status::OK) {
    return status;
  }

  auto& base_commit_root_id = commit_root_ids[0];
  auto& target_commit_root_id = commit_root_ids[1];

  // With full diff support, we expect the tree of the base to be locally present. In compatibility
  // mode, we use |ValueFromNetwork| as a location for the base, as it allows retrieving it as
  // objects if necessary, and avoids getting in a loop where we would retrieve it as a diff.
  // TODO(12356): remove compatibility flag.
  Location location =
      environment_->diff_compatibility_policy() == DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES
          ? Location::ValueFromNetwork()
          : Location::Local();
  ObjectIdentifier new_root_identifier;
  std::set<ObjectIdentifier> new_identifiers;
  status = btree::ApplyChangesFromCloud(handler, this,
                                        {base_commit_root_id, Location::ValueFromNetwork()},
                                        changes, &new_root_identifier, &new_identifiers);
  if (status != Status::OK) {
    return status;
  }
  // The new nodes created by |btree::ApplyChangesFromCloud| are marked as transient. This is the
  // state we want: we don't want them to be sent to the cloud now (because if we still send tree
  // objects, they are already in the cloud, and if we don't they don't need to be in the cloud),
  // but we might need some of their pieces to be sent if they become part of an object later on.

  if (new_root_identifier != target_commit_root_id) {
    LEDGER_LOG(ERROR)
        << "Applying the change provided by the cloud did not produce the expected tree.";
    return Status::INTERNAL_NOT_FOUND;
  }

  return Status::OK;
}

ObjectIdentifierFactory* PageStorageImpl::GetObjectIdentifierFactory() {
  return &object_identifier_factory_;
}

CommitFactory* PageStorageImpl::GetCommitFactory() { return &commit_factory_; }

Status PageStorageImpl::SynchronousInit(CoroutineHandler* handler,
                                        clocks::DeviceIdManager* device_id_manager) {
  // Add the default page head if this page is empty.
  std::vector<std::pair<zx::time_utc, CommitId>> heads;
  RETURN_ON_ERROR(db_->GetHeads(handler, &heads));
  // Cache the heads and update the live commit tracker.
  std::vector<std::unique_ptr<const Commit>> commits;
  if (heads.empty()) {
    RETURN_ON_ERROR(db_->AddHead(handler, kFirstPageCommitId, zx::time_utc()));
    std::unique_ptr<const Commit> head_commit;
    RETURN_ON_ERROR(
        SynchronousGetCommit(handler, convert::ToString(kFirstPageCommitId), &head_commit));
    commits.push_back(std::move(head_commit));
  } else {
    auto waiter =
        ledger::MakeRefCounted<ledger::Waiter<Status, std::unique_ptr<const Commit>>>(Status::OK);

    for (const auto& head : heads) {
      GetCommit(head.second, waiter->NewCallback());
    }
    Status status;
    if (coroutine::Wait(handler, std::move(waiter), &status, &commits) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    RETURN_ON_ERROR(status);
  }
  commit_factory_.AddHeads(std::move(commits));

  std::vector<std::unique_ptr<const Commit>> unsynced_commits;
  RETURN_ON_ERROR(SynchronousGetUnsyncedCommits(handler, &unsynced_commits));
  for (const auto& commit : unsynced_commits) {
    // When this |commit| will be synced to the cloud we will compute the diff from its base parent
    // commit: make sure the base's root identifier is not garbage collected.
    ObjectIdentifier base_parent_root;
    RETURN_ON_ERROR(GetBaseParentRootIdentifier(handler, *commit, &base_parent_root));
    commit_factory_.AddCommitDependencies(commit->GetId(),
                                          {commit->GetRootIdentifier(), base_parent_root});
  }

  clocks::DeviceId device_id;
  Status status = db_->GetDeviceId(handler, &device_id);
  if (status == Status::INTERNAL_NOT_FOUND) {
    RETURN_ON_ERROR(device_id_manager->GetNewDeviceId(handler, &device_id));
    RETURN_ON_ERROR(db_->SetDeviceId(handler, device_id));
  } else {
    RETURN_ON_ERROR(status);
  }

  Clock clock;
  status = db_->GetClock(handler, &clock);
  if (status == Status::INTERNAL_NOT_FOUND) {
    RETURN_ON_ERROR(db_->SetClock(handler, clock));
  } else {
    RETURN_ON_ERROR(status);
  }
  commit_pruner_.LoadClock(std::move(device_id), std::move(clock));

  object_identifier_factory_.SetUntrackedCallback(
      callback::MakeScoped(weak_factory_.GetWeakPtr(), [this](const ObjectDigest& object_digest) {
        ScheduleObjectGarbageCollection(object_digest);
      }));

  // Cache whether this page is online or not.
  return db_->IsPageOnline(handler, &page_is_online_);
}

Status PageStorageImpl::SynchronousGetCommit(CoroutineHandler* handler, CommitId commit_id,
                                             std::unique_ptr<const Commit>* commit) {
  if (IsFirstCommit(commit_id)) {
    Status s;
    if (coroutine::SyncCall(
            handler,
            [this](fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
              commit_factory_.Empty(this, std::move(callback));
            },
            &s, commit) == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    return s;
  }
  std::string bytes;
  RETURN_ON_ERROR(db_->GetCommitStorageBytes(handler, commit_id, &bytes));
  return commit_factory_.FromStorageBytes(commit_id, std::move(bytes), commit);
}

Status PageStorageImpl::SynchronousAddCommitFromLocal(CoroutineHandler* handler,
                                                      std::unique_ptr<const Commit> commit,
                                                      std::vector<ObjectIdentifier> new_objects) {
  LEDGER_DCHECK(IsDigestValid(commit->GetRootIdentifier().object_digest()));
  LEDGER_DCHECK(IsTokenValid(commit->GetRootIdentifier()));
  std::vector<std::unique_ptr<const Commit>> commits;
  commits.reserve(1);
  commits.push_back(std::move(commit));

  return SynchronousAddCommits(handler, std::move(commits), ChangeSource::LOCAL,
                               std::move(new_objects));
}

Status PageStorageImpl::SynchronousAddCommitsFromSync(CoroutineHandler* handler,
                                                      std::vector<CommitIdAndBytes> ids_and_bytes,
                                                      ChangeSource source) {
  std::vector<std::unique_ptr<const Commit>> commits;

  // The set of commit whose objects we have to download. If |source| is |ChangeSource::CLOUD|, we
  // only need to get the heads. If |source| is |ChangeSource::P2P|, we must get all objects from
  // unsynced commits, because we might have to upload them to the cloud.
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

    std::unique_ptr<const Commit> commit;
    Status status = SynchronousGetCommit(handler, id, &commit);
    if (status == Status::OK) {
      // We only mark cloud-sourced commits as synced.
      if (source == ChangeSource::CLOUD) {
        RETURN_ON_ERROR(SynchronousMarkCommitSynced(handler, *commit));
      }
      continue;
    }

    if (status != Status::INTERNAL_NOT_FOUND) {
      return status;
    }

    status = commit_factory_.FromStorageBytes(id, std::move(storage_bytes), &commit);
    if (status != Status::OK) {
      LEDGER_LOG(ERROR) << "Unable to add commit. Id: " << convert::ToHex(id);
      return status;
    }

    // For commits from the cloud, remove parents from leaves.
    // TODO(35279): send sync information with P2P commits so we can remove all synced parents from
    // the leaves.
    if (source == ChangeSource::CLOUD) {
      for (const auto& parent_id : commit->GetParentIds()) {
        auto it = leaves.find(&parent_id);
        if (it != leaves.end()) {
          leaves.erase(it);
        }
      }
    }
    leaves[&commit->GetId()] = commit.get();
    commits.push_back(std::move(commit));
  }

  if (commits.empty()) {
    return Status::OK;
  }

  lock.reset();

  // Register the commits as being added, so their CommitId/root ObjectIdentifier is available to
  // GetObject.
  // TODO(12356): Once compatibility is not necessary, we can use |Location| to store this
  // information instead.
  std::vector<CommitId> commit_ids_being_added;
  for (const auto& commit : commits) {
    commit_ids_being_added.push_back(commit->GetId());
    roots_of_commits_being_added_[commit->GetId()] = commit->GetRootIdentifier();
    remote_ids_of_commits_being_added_[encryption_service_->EncodeCommitId(commit->GetId())] =
        commit->GetId();
  }

  // If the commits come from P2P, we may have to sync them. This requires having the tree of their
  // parents locally present.
  // This vector keeps the roots of the parents to download alive: it must not be cleared before the
  // commits are added.
  // TODO(35279): send sync information with P2P commits so we don't do this for synced commits.
  std::vector<btree::LocatedObjectIdentifier> parents_to_download;
  if (source == ChangeSource::P2P) {
    for (const auto& commit : commits) {
      if (leaves.find(&commit->GetParentIds()[0]) == leaves.end()) {
        ObjectIdentifier base_parent_root;
        RETURN_ON_ERROR(GetBaseParentRootIdentifier(handler, *commit, &base_parent_root));
        parents_to_download.push_back(
            {std::move(base_parent_root), PageStorage::Location::TreeNodeFromNetwork(
                                              convert::ToString(commit->GetParentIds()[0]))});
      }
    }
  }

  auto waiter = ledger::MakeRefCounted<ledger::StatusWaiter<Status>>(Status::OK);
  // Get all objects from sync and then add the commit objects.
  for (const auto& leaf : leaves) {
    btree::GetObjectsFromSync(environment_->coroutine_service(), this,
                              {leaf.second->GetRootIdentifier(),
                               PageStorage::Location::TreeNodeFromNetwork(leaf.second->GetId())},
                              waiter->NewCallback());
  }
  for (const auto& root : parents_to_download) {
    // List the entries. This ensures the whole tree is present locally.
    btree::ForEachEntry(
        environment_->coroutine_service(), this, root, "", [](Entry e) { return true; },
        waiter->NewCallback());
  }

  Status waiter_status;
  if (coroutine::Wait(handler, std::move(waiter), &waiter_status) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (waiter_status != Status::OK) {
    return waiter_status;
  }

  Status status =
      SynchronousAddCommits(handler, std::move(commits), source, std::vector<ObjectIdentifier>());
  if (status == Status::OK) {
    // We only remove the commits from the map once they have been successfully added to storage:
    // this ensures we never lose a CommitId / root ObjectIdentifier association.
    for (auto commit_id : commit_ids_being_added) {
      roots_of_commits_being_added_.erase(commit_id);
      remote_ids_of_commits_being_added_.erase(encryption_service_->EncodeCommitId(commit_id));
    }
  }
  return status;
}

Status PageStorageImpl::SynchronousGetUnsyncedCommits(
    CoroutineHandler* handler, std::vector<std::unique_ptr<const Commit>>* unsynced_commits) {
  std::vector<CommitId> commit_ids;
  RETURN_ON_ERROR(db_->GetUnsyncedCommitIds(handler, &commit_ids));

  auto waiter =
      ledger::MakeRefCounted<ledger::Waiter<Status, std::unique_ptr<const Commit>>>(Status::OK);
  for (const auto& commit_id : commit_ids) {
    GetCommit(commit_id, waiter->NewCallback());
  }

  Status status;
  std::vector<std::unique_ptr<const Commit>> result;
  if (coroutine::Wait(handler, std::move(waiter), &status, &result) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);
  unsynced_commits->swap(result);
  return Status::OK;
}

Status PageStorageImpl::SynchronousMarkCommitSynced(CoroutineHandler* handler,
                                                    const Commit& commit) {
  std::unique_ptr<PageDb::Batch> batch;
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  RETURN_ON_ERROR(SynchronousMarkCommitSyncedInBatch(handler, batch.get(), commit.GetId()));
  Status status = batch->Execute(handler);
  if (status == Status::OK && commit.GetId() != kFirstPageCommitId) {
    commit_factory_.RemoveCommitDependencies(commit.GetId());
    object_identifier_factory_.NotifyOnUntracked(commit.GetRootIdentifier().object_digest());
  }
  return status;
}

Status PageStorageImpl::SynchronousMarkCommitSyncedInBatch(CoroutineHandler* handler,
                                                           PageDb::Batch* batch,
                                                           const CommitId& commit_id) {
  RETURN_ON_ERROR(SynchronousMarkPageOnline(handler, batch));
  return batch->MarkCommitIdSynced(handler, commit_id);
}

Status PageStorageImpl::SynchronousAddCommits(CoroutineHandler* handler,
                                              std::vector<std::unique_ptr<const Commit>> commits,
                                              ChangeSource source,
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
  RETURN_ON_ERROR(db_->StartBatch(handler, &batch));
  std::vector<std::unique_ptr<const Commit>> commits_to_send;

  std::map<CommitId, std::unique_ptr<const Commit>> heads_to_add;
  std::vector<CommitId> removed_heads;
  // Contains the ids of the synced commits along with the ObjectIdentifier of their root.
  std::vector<std::pair<CommitId, ObjectIdentifier>> synced_commits;
  // Contains the ids of the unsynced commits along with their dependent ObjectIdentifiers, i.e.
  // those of their root node, and of the root node of their base parent.
  std::vector<std::pair<CommitId, std::vector<ObjectIdentifier>>> unsynced_commits;
  std::map<const CommitId*, const Commit*, StringPointerComparator> id_to_commit_map;

  for (auto& commit : commits) {
    // We need to check if we are adding an already present remote commit here
    // because we might both download and locally commit the same commit at
    // roughly the same time. As commit writing is asynchronous, the previous
    // check in AddCommitsFromSync may have not matched any commit, while a
    // commit got added in between.
    Status s = ContainsCommit(handler, commit->GetId());
    if (s == Status::OK) {
      if (source == ChangeSource::CLOUD) {
        RETURN_ON_ERROR(SynchronousMarkCommitSyncedInBatch(handler, batch.get(), commit->GetId()));
        // Synced commits will need to be removed from the commit factory once the batch is executed
        // successfully.
        if (commit->GetId() != kFirstPageCommitId) {
          synced_commits.push_back({commit->GetId(), commit->GetRootIdentifier()});
        }
      }
      // The commit is already here. We can safely skip it.
      continue;
    }
    if (s != Status::INTERNAL_NOT_FOUND) {
      return s;
    }
    // Now, we know we are adding a new commit.

    // If the commit is a merge, register it in the merge index.
    std::vector<CommitIdView> parent_ids = commit->GetParentIds();
    if (parent_ids.size() == 2) {
      RETURN_ON_ERROR(batch->AddMerge(handler, parent_ids[0], parent_ids[1], commit->GetId()));
    }

    // Commits should arrive in order. Check that the parents are either
    // present in PageDb or in the list of already processed commits.
    for (const CommitIdView& parent_id : parent_ids) {
      if (id_to_commit_map.find(&parent_id) == id_to_commit_map.end()) {
        s = ContainsCommit(handler, parent_id);
        if (s == Status::INTERRUPTED) {
          return s;
        }
        if (s != Status::OK) {
          LEDGER_LOG(ERROR) << "Failed to find parent commit \"" << ToHex(parent_id)
                            << "\" of commit \"" << convert::ToHex(commit->GetId()) << "\".";
          if (s == Status::INTERNAL_NOT_FOUND) {
            ledger::ReportEvent(ledger::CobaltEvent::COMMITS_RECEIVED_OUT_OF_ORDER_NOT_RECOVERED);
            return s;
          } else {
            return Status::INTERNAL_ERROR;
          }
        }
      }
      // Remove the parent from the list of heads.
      if (!heads_to_add.erase(convert::ToString(parent_id))) {
        // parent_id was not added in the batch: remove it from heads in Db.
        RETURN_ON_ERROR(batch->RemoveHead(handler, parent_id));
        removed_heads.push_back(convert::ToString(parent_id));
      }
    }

    RETURN_ON_ERROR(batch->AddCommitStorageBytes(
        handler, commit->GetId(), encryption_service_->EncodeCommitId(commit->GetId()),
        commit->GetRootIdentifier(), commit->GetStorageBytes()));

    if (source != ChangeSource::CLOUD) {
      // New commits from LOCAL or P2P are unsynced. They will be added to the commit factory once
      // the batch is executed successfully.
      RETURN_ON_ERROR(
          batch->MarkCommitIdUnsynced(handler, commit->GetId(), commit->GetGeneration()));
      ObjectIdentifier base_parent_root;
      auto it = id_to_commit_map.find(&commit->GetParentIds()[0]);
      if (it != id_to_commit_map.end()) {
        base_parent_root = it->second->GetRootIdentifier();
      } else {
        RETURN_ON_ERROR(GetBaseParentRootIdentifier(handler, *commit, &base_parent_root));
      }
      unsynced_commits.push_back(
          {commit->GetId(), {commit->GetRootIdentifier(), base_parent_root}});
    }

    // Update heads_to_add.
    heads_to_add[commit->GetId()] = commit->Clone();

    id_to_commit_map[&commit->GetId()] = commit.get();
    commits_to_send.push_back(std::move(commit));
  }

  // Update heads in Db.
  for (const auto& head : heads_to_add) {
    RETURN_ON_ERROR(batch->AddHead(handler, head.second->GetId(), head.second->GetTimestamp()));
  }

  // If adding local commits, mark all new pieces as local. It is safe to discard |new_objects| at
  // this point, because only the root piece of each commit needs to be kept alive (other pieces
  // down the tree have on-disk references to each other), and each is referenced by its respective
  // commit in |commits| or |commits_to_send|.
  RETURN_ON_ERROR(MarkAllPiecesLocal(handler, batch.get(), std::move(new_objects)));
  RETURN_ON_ERROR(batch->Execute(handler));

  // If these commits came from the cloud, they are marked as synced and we should remove them from
  // the commit factory. If they came from P2P or local they are marked as unsynced and should
  // instead be added in commit factory. Check that at most one of these containers has elements.
  LEDGER_DCHECK(synced_commits.empty() || unsynced_commits.empty());

  // Remove all synced commits from the commit_factory_.
  for (const auto& [synced_commit_id, root_identifier] : synced_commits) {
    commit_factory_.RemoveCommitDependencies(synced_commit_id);
    object_identifier_factory_.NotifyOnUntracked(root_identifier.object_digest());
  }
  // Add all unsynced commits to the commit_factory_.
  for (const auto& [unsynced_commit_id, identifiers] : unsynced_commits) {
    commit_factory_.AddCommitDependencies(unsynced_commit_id, identifiers);
  }

  // Only update the cache of heads after a successful update of the PageDb.
  commit_factory_.RemoveHeads(std::move(removed_heads));
  std::vector<std::unique_ptr<const Commit>> new_heads;
  std::transform(std::make_move_iterator(heads_to_add.begin()),
                 std::make_move_iterator(heads_to_add.end()), std::back_inserter(new_heads),
                 [](std::pair<CommitId, std::unique_ptr<const Commit>>&& head) {
                   return std::move(std::get<std::unique_ptr<const Commit>>(head));
                 });
  commit_factory_.AddHeads(std::move(new_heads));
  NotifyWatchersOfNewCommits(commits_to_send, source);

  commit_pruner_.SchedulePruning();
  return Status::OK;
}

Status PageStorageImpl::SynchronousAddPiece(CoroutineHandler* handler, const Piece& piece,
                                            ChangeSource source, IsObjectSynced is_object_synced,
                                            ObjectReferencesAndPriority references) {
  LEDGER_DCHECK(!GetObjectDigestInfo(piece.GetIdentifier().object_digest()).is_inlined());
  LEDGER_DCHECK(
      piece.GetIdentifier().object_digest() ==
      ComputeObjectDigest(GetObjectDigestInfo(piece.GetIdentifier().object_digest()).piece_type,
                          GetObjectDigestInfo(piece.GetIdentifier().object_digest()).object_type,
                          piece.GetData()));

  Status status = db_->HasObject(handler, piece.GetIdentifier());
  if (status == Status::INTERNAL_NOT_FOUND) {
    PageDbObjectStatus object_status;
    switch (is_object_synced) {
      case IsObjectSynced::NO:
        object_status = (source == ChangeSource::LOCAL ? PageDbObjectStatus::TRANSIENT
                                                       : PageDbObjectStatus::LOCAL);
        break;
      case IsObjectSynced::YES:
        object_status = PageDbObjectStatus::SYNCED;
        break;
    }
    return db_->WriteObject(handler, piece, object_status, references);
  }
  return status;
}

Status PageStorageImpl::SynchronousMarkPageOnline(coroutine::CoroutineHandler* handler,
                                                  PageDb::Batch* batch) {
  if (page_is_online_) {
    return Status::OK;
  }
  Status status = batch->MarkPageOnline(handler);
  if (status == Status::OK) {
    page_is_online_ = true;
  }
  return status;
}

ABSL_MUST_USE_RESULT Status PageStorageImpl::SynchronousGetEmptyNodeIdentifier(
    coroutine::CoroutineHandler* handler, ObjectIdentifier** empty_node_id) {
  if (!empty_node_id_) {
    // Get the empty node identifier and cache it.
    Status status;
    ObjectIdentifier object_identifier;
    if (coroutine::SyncCall(
            handler,
            [this](fit::function<void(Status, ObjectIdentifier)> callback) {
              btree::TreeNode::Empty(this, std::move(callback));
            },
            &status, &object_identifier) == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    RETURN_ON_ERROR(status);
    empty_node_id_ = std::make_unique<ObjectIdentifier>(std::move(object_identifier));
  }
  *empty_node_id = empty_node_id_.get();
  return Status::OK;
}

ABSL_MUST_USE_RESULT Status PageStorageImpl::GetBaseParentRootIdentifier(
    coroutine::CoroutineHandler* handler, const Commit& commit,
    ObjectIdentifier* base_parent_root) {
  std::unique_ptr<const Commit> base_parent;
  RETURN_ON_ERROR(
      SynchronousGetCommit(handler, convert::ToString(commit.GetParentIds()[0]), &base_parent));
  *base_parent_root = base_parent->GetRootIdentifier();
  return Status::OK;
}

bool PageStorageImpl::IsTokenValid(const ObjectIdentifier& object_identifier) {
  return object_identifier.factory() == &object_identifier_factory_;
}

void PageStorageImpl::ChooseDiffBases(CommitIdView target_id,
                                      fit::callback<void(Status, std::vector<CommitId>)> callback) {
  // We find the synced heads by looking at the heads and the unsynced commits. As long as we do not
  // get synced status by P2P, we are sure that the tree of these commits is present locally.
  // TODO(ambre): implement a smarter version.

  std::vector<std::unique_ptr<const Commit>> heads;
  Status status = GetHeadCommits(&heads);
  if (status != Status::OK) {
    callback(status, {});
    return;
  }

  GetUnsyncedCommits(
      [heads = std::move(heads), callback = std::move(callback)](
          Status status, std::vector<std::unique_ptr<const Commit>> unsynced_commits) mutable {
        if (status != Status::OK) {
          callback(status, {});
          return;
        }

        // The sync heads are either heads or parents of unsynced commits, and are not unsynced
        // commits themselves.
        std::set<CommitId> sync_head_ids;
        for (const auto& head : heads) {
          sync_head_ids.insert(head->GetId());
        }
        for (const auto& commit : unsynced_commits) {
          for (const auto& parent_id : commit->GetParentIds()) {
            sync_head_ids.insert(convert::ToString(parent_id));
          }
        }
        for (const auto& commit : unsynced_commits) {
          sync_head_ids.erase(commit->GetId());
        }

        std::vector<CommitId> diff_bases;
        diff_bases.reserve(sync_head_ids.size());
        std::move(sync_head_ids.begin(), sync_head_ids.end(), std::back_inserter(diff_bases));
        callback(Status::OK, std::move(diff_bases));
      });
}

}  // namespace storage
