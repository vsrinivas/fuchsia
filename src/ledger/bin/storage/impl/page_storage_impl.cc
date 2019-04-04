// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_storage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/callback/trace_callback.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/memory/ref_ptr.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <trace/event.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/bin/lock/lock.h"
#include "src/ledger/bin/storage/impl/btree/diff.h"
#include "src/ledger/bin/storage/impl/btree/iterator.h"
#include "src/ledger/bin/storage/impl/commit_impl.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/impl/journal_impl.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/split.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/file_descriptor.h"
#include "src/lib/files/path.h"
#include "src/lib/files/unique_fd.h"

namespace storage {

using coroutine::CoroutineHandler;

namespace {

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

int64_t GetObjectPartLength(int64_t max_size, int64_t object_size,
                            int64_t start) {
  int64_t adjusted_max_size = max_size < 0 ? object_size : max_size;
  return start > object_size ? 0
                             : std::min(adjusted_max_size, object_size - start);
}

}  // namespace

PageStorageImpl::PageStorageImpl(
    ledger::Environment* environment,
    encryption::EncryptionService* encryption_service, std::unique_ptr<Db> db,
    PageId page_id)
    : PageStorageImpl(environment, encryption_service,
                      std::make_unique<PageDbImpl>(environment, std::move(db)),
                      std::move(page_id)) {}

PageStorageImpl::PageStorageImpl(
    ledger::Environment* environment,
    encryption::EncryptionService* encryption_service,
    std::unique_ptr<PageDb> page_db, PageId page_id)
    : environment_(environment),
      encryption_service_(encryption_service),
      page_id_(std::move(page_id)),
      db_(std::move(page_db)),
      page_sync_(nullptr),
      coroutine_manager_(environment->coroutine_service()) {}

PageStorageImpl::~PageStorageImpl() {}

void PageStorageImpl::Init(fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(SynchronousInit(handler));
      });
}

PageId PageStorageImpl::GetId() { return page_id_; }

void PageStorageImpl::SetSyncDelegate(PageSyncDelegate* page_sync) {
  page_sync_ = page_sync;
}

Status PageStorageImpl::GetHeadCommits(
    std::vector<std::unique_ptr<const Commit>>* head_commits) {
  FXL_DCHECK(head_commits);
  *head_commits = commit_tracker_.GetHeads();
  return Status::OK;
}

void PageStorageImpl::GetMergeCommitIds(
    CommitIdView parent1_id, CommitIdView parent2_id,
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, parent1_id = parent1_id.ToString(),
       parent2_id = parent2_id.ToString()](
          CoroutineHandler* handler,
          fit::function<void(Status, std::vector<CommitId>)> callback) {
        std::vector<CommitId> commit_ids;
        Status status =
            db_->GetMerges(handler, parent1_id, parent2_id, &commit_ids);
        callback(status, std::move(commit_ids));
      });
}

void PageStorageImpl::GetCommit(
    CommitIdView commit_id,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  FXL_DCHECK(commit_id.size());
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, commit_id = commit_id.ToString()](
          CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
        std::unique_ptr<const Commit> commit;
        Status status = SynchronousGetCommit(handler, commit_id, &commit);
        callback(status, std::move(commit));
      });
}

void PageStorageImpl::AddCommitFromLocal(
    std::unique_ptr<const Commit> commit,
    std::vector<ObjectIdentifier> new_objects,
    fit::function<void(Status)> callback) {
  FXL_DCHECK(IsDigestValid(commit->GetRootIdentifier().object_digest()));
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, commit = std::move(commit), new_objects = std::move(new_objects)](
          CoroutineHandler* handler,
          fit::function<void(Status)> callback) mutable {
        callback(SynchronousAddCommitFromLocal(handler, std::move(commit),
                                               std::move(new_objects)));
      });
}

void PageStorageImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, ids_and_bytes = std::move(ids_and_bytes), source](
          CoroutineHandler* handler,
          fit::function<void(Status, std::vector<CommitId>)> callback) mutable {
        std::vector<CommitId> missing_ids;
        Status status = SynchronousAddCommitsFromSync(
            handler, std::move(ids_and_bytes), source, &missing_ids);
        callback(status, std::move(missing_ids));
      });
}

std::unique_ptr<Journal> PageStorageImpl::StartCommit(
    std::unique_ptr<const Commit> commit) {
  return JournalImpl::Simple(environment_, this, std::move(commit));
}

std::unique_ptr<Journal> PageStorageImpl::StartMergeCommit(
    std::unique_ptr<const Commit> left, std::unique_ptr<const Commit> right) {
  return JournalImpl::Merge(environment_, this, std::move(left),
                            std::move(right));
}

void PageStorageImpl::CommitJournal(
    std::unique_ptr<Journal> journal,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  FXL_DCHECK(journal);

  auto managed_journal = managed_container_.Manage(std::move(journal));
  JournalImpl* journal_ptr = static_cast<JournalImpl*>(managed_journal->get());

  // |managed_journal| needs to be kept in memory until |Commit| has finished
  // execution.
  journal_ptr->Commit(
      [managed_journal = std::move(managed_journal),
       callback = std::move(callback)](
          Status status, std::unique_ptr<const Commit> commit) mutable {
        callback(status, std::move(commit));
      });
}

void PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) {
  watchers_.AddObserver(watcher);
}

void PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  watchers_.RemoveObserver(watcher);
}

void PageStorageImpl::IsSynced(fit::function<void(Status, bool)> callback) {
  auto waiter = fxl::MakeRefCounted<callback::Waiter<Status, bool>>(Status::OK);
  // Check for unsynced commits.
  coroutine_manager_.StartCoroutine(
      waiter->NewCallback(),
      [this](CoroutineHandler* handler,
             fit::function<void(Status, bool)> callback) {
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

bool PageStorageImpl::IsOnline() { return page_is_online_; }

void PageStorageImpl::IsEmpty(fit::function<void(Status, bool)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this](CoroutineHandler* handler,
                                  fit::function<void(Status, bool)> callback) {
        // Check there is a single head.
        std::vector<std::pair<zx::time_utc, CommitId>> commit_ids;
        Status status = db_->GetHeads(handler, &commit_ids);
        if (status != Status::OK) {
          callback(status, false);
          return;
        }
        FXL_DCHECK(!commit_ids.empty());
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
    fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
        callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](CoroutineHandler* handler,
             fit::function<void(Status,
                                std::vector<std::unique_ptr<const Commit>>)>
                 callback) {
        std::vector<std::unique_ptr<const Commit>> unsynced_commits;
        Status s = SynchronousGetUnsyncedCommits(handler, &unsynced_commits);
        callback(s, std::move(unsynced_commits));
      });
}

void PageStorageImpl::MarkCommitSynced(const CommitId& commit_id,
                                       fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, commit_id](CoroutineHandler* handler,
                        fit::function<void(Status)> callback) {
        callback(SynchronousMarkCommitSynced(handler, commit_id));
      });
}

void PageStorageImpl::GetUnsyncedPieces(
    fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this](
          CoroutineHandler* handler,
          fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
        std::vector<ObjectIdentifier> unsynced_object_identifiers;
        Status s =
            db_->GetUnsyncedPieces(handler, &unsynced_object_identifiers);
        callback(s, unsynced_object_identifiers);
      });
}

void PageStorageImpl::MarkPieceSynced(ObjectIdentifier object_identifier,
                                      fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(db_->SetObjectStatus(handler, object_identifier,
                                      PageDbObjectStatus::SYNCED));
      });
}

void PageStorageImpl::IsPieceSynced(
    ObjectIdentifier object_identifier,
    fit::function<void(Status, bool)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler,
          fit::function<void(Status, bool)> callback) {
        PageDbObjectStatus object_status;
        Status status =
            db_->GetObjectStatus(handler, object_identifier, &object_status);
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

void PageStorageImpl::AddObjectFromLocal(
    ObjectType object_type, std::unique_ptr<DataSource> data_source,
    ObjectReferencesAndPriority tree_references,
    fit::function<void(Status, ObjectIdentifier)> callback) {
  // |data_source| is not splitted yet: |tree_references| must contain only
  // BTree-level references, not piece-level references, and only in the case
  // where |data_source| actually represents a tree node.
  FXL_DCHECK(object_type == ObjectType::TREE_NODE || tree_references.empty());
  auto traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_add_object");

  auto managed_data_source = managed_container_.Manage(std::move(data_source));
  auto managed_data_source_ptr = managed_data_source->get();
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  SplitDataSource(
      managed_data_source_ptr, object_type,
      [this](ObjectDigest object_digest) {
        FXL_DCHECK(IsDigestValid(object_digest));
        return encryption_service_->MakeObjectIdentifier(
            std::move(object_digest));
      },
      [this, waiter, managed_data_source = std::move(managed_data_source),
       tree_references = std::move(tree_references),
       callback = std::move(traced_callback)](
          IterationStatus status, std::unique_ptr<Piece> piece,
          ObjectReferencesAndPriority piece_references) mutable {
        if (status == IterationStatus::ERROR) {
          callback(Status::IO_ERROR, ObjectIdentifier());
          return;
        }

        FXL_DCHECK(piece != nullptr);

        ObjectIdentifier identifier = piece->GetIdentifier();
        auto object_info = GetObjectDigestInfo(identifier.object_digest());
        if (!object_info.is_inlined()) {
          if (object_info.object_type == ObjectType::TREE_NODE) {
            // There is at most one TREE_NODE, and it must be the last piece, so
            // it is safe to add tree_references to piece_references there.
            FXL_DCHECK(status == IterationStatus::DONE);
            piece_references.insert(
                std::make_move_iterator(tree_references.begin()),
                std::make_move_iterator(tree_references.end()));
          }
          AddPiece(std::move(piece), ChangeSource::LOCAL, IsObjectSynced::NO,
                   std::move(piece_references), waiter->NewCallback());
        }
        if (status == IterationStatus::IN_PROGRESS)
          return;

        FXL_DCHECK(status == IterationStatus::DONE);
        waiter->Finalize(
            [identifier = std::move(identifier),
             callback = std::move(callback)](Status status) mutable {
              callback(status, std::move(identifier));
            });
      });
}

void PageStorageImpl::GetObjectPart(
    ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
    Location location, fit::function<void(Status, fsl::SizedVmo)> callback) {
  FXL_DCHECK(IsDigestValid(object_identifier.object_digest()));
  GetPiece(object_identifier, [this, object_identifier, offset, max_size,
                               location, callback = std::move(callback)](
                                  Status status,
                                  std::unique_ptr<const Piece> piece) mutable {
    if (status == Status::INTERNAL_NOT_FOUND) {
      // The root piece is missing; download the whole object (if we
      // have network).
      if (location == Location::NETWORK) {
        DownloadObjectPart(
            object_identifier, offset, max_size, 0,
            [this, object_identifier, offset, max_size, location,
             callback = std::move(callback)](Status status) mutable {
              if (status != Status::OK) {
                callback(status, nullptr);
                return;
              }

              GetObjectPart(object_identifier, offset, max_size, location,
                            std::move(callback));
            });
      } else {
        callback(Status::INTERNAL_NOT_FOUND, nullptr);
      }
      return;
    }

    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    FXL_DCHECK(piece);
    ObjectDigestInfo digest_info =
        GetObjectDigestInfo(object_identifier.object_digest());
    fxl::StringView data = piece->GetData();

    if (digest_info.is_inlined() || digest_info.is_chunk()) {
      fsl::SizedVmo buffer;
      int64_t start = GetObjectPartStart(offset, data.size());
      int64_t length = GetObjectPartLength(max_size, data.size(), start);

      if (!fsl::VmoFromString(data.substr(start, length), &buffer)) {
        callback(Status::INTERNAL_ERROR, nullptr);
        return;
      }
      callback(Status::OK, std::move(buffer));
      return;
    }

    FXL_DCHECK(digest_info.piece_type == PieceType::INDEX);
    GetIndexObject(std::move(piece), offset, max_size, location,
                   std::move(callback));
  });
}

void PageStorageImpl::GetObject(
    ObjectIdentifier object_identifier, Location location,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  auto traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_get_object");
  FXL_DCHECK(IsDigestValid(object_identifier.object_digest()));
  GetPiece(object_identifier, [this, object_identifier, location,
                               callback = std::move(traced_callback)](
                                  Status status,
                                  std::unique_ptr<const Piece> piece) mutable {
    if (status == Status::INTERNAL_NOT_FOUND) {
      // The root piece is missing; download it (if we have network),
      // as well as all the descendent pieces that we might need to
      // read the requested part.
      if (location == Location::NETWORK) {
        DownloadObjectPart(
            object_identifier, 0, -1, 0,
            [this, object_identifier, location,
             callback = std::move(callback)](Status status) mutable {
              if (status != Status::OK) {
                callback(status, nullptr);
                return;
              }

              GetObject(object_identifier, location, std::move(callback));
            });
      } else {
        callback(Status::INTERNAL_NOT_FOUND, nullptr);
      }
      return;
    }

    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }

    FXL_DCHECK(piece);
    ObjectDigestInfo digest_info =
        GetObjectDigestInfo(object_identifier.object_digest());

    if (digest_info.is_chunk()) {
      callback(Status::OK, std::make_unique<ChunkObject>(std::move(piece)));
      return;
    }

    FXL_DCHECK(digest_info.piece_type == PieceType::INDEX);
    GetIndexObject(
        std::move(piece), 0, -1, location,
        [object_identifier, callback = std::move(callback)](
            Status status, fsl::SizedVmo vmo) mutable {
          callback(status, std::make_unique<VmoObject>(
                               std::move(object_identifier), std::move(vmo)));
        }

    );
  });
}

void PageStorageImpl::GetPiece(
    ObjectIdentifier object_identifier,
    fit::function<void(Status, std::unique_ptr<const Piece>)> callback) {
  ObjectDigestInfo digest_info =
      GetObjectDigestInfo(object_identifier.object_digest());
  if (digest_info.is_inlined()) {
    callback(Status::OK,
             std::make_unique<InlinePiece>(std::move(object_identifier)));
    return;
  }

  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler,
          fit::function<void(Status, std::unique_ptr<const Piece>)>
              callback) mutable {
        std::unique_ptr<const Piece> piece;
        Status status =
            db_->ReadObject(handler, std::move(object_identifier), &piece);
        callback(status, std::move(piece));
      });
}

void PageStorageImpl::SetSyncMetadata(fxl::StringView key,
                                      fxl::StringView value,
                                      fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, key = key.ToString(), value = value.ToString()](
          CoroutineHandler* handler, fit::function<void(Status)> callback) {
        callback(db_->SetSyncMetadata(handler, key, value));
      });
}

void PageStorageImpl::GetSyncMetadata(
    fxl::StringView key, fit::function<void(Status, std::string)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, key = key.ToString()](
          CoroutineHandler* handler,
          fit::function<void(Status, std::string)> callback) {
        std::string value;
        Status status = db_->GetSyncMetadata(handler, key, &value);
        callback(status, std::move(value));
      });
}

void PageStorageImpl::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        fit::function<bool(Entry)> on_next,
                                        fit::function<void(Status)> on_done) {
  btree::ForEachEntry(
      environment_->coroutine_service(), this, commit.GetRootIdentifier(),
      min_key,
      [on_next = std::move(on_next)](btree::EntryAndNodeIdentifier next) {
        return on_next(next.entry);
      },
      std::move(on_done));
}

void PageStorageImpl::GetEntryFromCommit(
    const Commit& commit, std::string key,
    fit::function<void(Status, Entry)> callback) {
  std::unique_ptr<bool> key_found = std::make_unique<bool>(false);
  auto on_next = [key, key_found = key_found.get(),
                  callback =
                      callback.share()](btree::EntryAndNodeIdentifier next) {
    if (next.entry.key == key) {
      *key_found = true;
      callback(Status::OK, next.entry);
    }
    return false;
  };

  auto on_done = [key_found = std::move(key_found),
                  callback = std::move(callback)](Status s) {
    if (*key_found) {
      return;
    }
    if (s == Status::OK) {
      callback(Status::KEY_NOT_FOUND, Entry());
      return;
    }
    callback(s, Entry());
  };
  btree::ForEachEntry(environment_->coroutine_service(), this,
                      commit.GetRootIdentifier(), std::move(key),
                      std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetCommitContentsDiff(
    const Commit& base_commit, const Commit& other_commit, std::string min_key,
    fit::function<bool(EntryChange)> on_next_diff,
    fit::function<void(Status)> on_done) {
  btree::ForEachDiff(environment_->coroutine_service(), this,
                     base_commit.GetRootIdentifier(),
                     other_commit.GetRootIdentifier(), std::move(min_key),
                     std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::GetThreeWayContentsDiff(
    const Commit& base_commit, const Commit& left_commit,
    const Commit& right_commit, std::string min_key,
    fit::function<bool(ThreeWayChange)> on_next_diff,
    fit::function<void(Status)> on_done) {
  btree::ForEachThreeWayDiff(
      environment_->coroutine_service(), this, base_commit.GetRootIdentifier(),
      left_commit.GetRootIdentifier(), right_commit.GetRootIdentifier(),
      std::move(min_key), std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::NotifyWatchersOfNewCommits(
    const std::vector<std::unique_ptr<const Commit>>& new_commits,
    ChangeSource source) {
  for (auto& watcher : watchers_) {
    watcher.OnNewCommits(new_commits, source);
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
    FXL_DCHECK(
        !GetObjectDigestInfo(object_identifier.object_digest()).is_inlined());
    Status status = batch->SetObjectStatus(handler, object_identifier,
                                           PageDbObjectStatus::LOCAL);
    if (status != Status::OK) {
      return status;
    }
    if (GetObjectDigestInfo(object_identifier.object_digest()).piece_type ==
        PieceType::INDEX) {
      std::unique_ptr<const Piece> piece;
      status = db_->ReadObject(handler, object_identifier, &piece);
      if (status != Status::OK) {
        return status;
      }

      fxl::StringView content = piece->GetData();

      const FileIndex* file_index;
      status = FileIndexSerialization::ParseFileIndex(content, &file_index);
      if (status != Status::OK) {
        return status;
      }

      object_identifiers.reserve(object_identifiers.size() +
                                 file_index->children()->size());
      for (const auto* child : *file_index->children()) {
        ObjectIdentifier new_object_identifier =
            ToObjectIdentifier(child->object_identifier());
        if (!GetObjectDigestInfo(new_object_identifier.object_digest())
                 .is_inlined()) {
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

void PageStorageImpl::AddPiece(std::unique_ptr<Piece> piece,
                               ChangeSource source,
                               IsObjectSynced is_object_synced,
                               ObjectReferencesAndPriority references,
                               fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, piece = std::move(piece), source,
       references = std::move(references),
       is_object_synced](CoroutineHandler* handler,
                         fit::function<void(Status)> callback) mutable {
        callback(SynchronousAddPiece(handler, *piece, source, is_object_synced,
                                     std::move(references)));
      });
}

void PageStorageImpl::ObjectIsUntracked(
    ObjectIdentifier object_identifier,
    fit::function<void(Status, bool)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier)](
          CoroutineHandler* handler,
          fit::function<void(Status, bool)> callback) mutable {
        if (GetObjectDigestInfo(object_identifier.object_digest())
                .is_inlined()) {
          callback(Status::OK, false);
          return;
        }

        PageDbObjectStatus object_status;
        Status status =
            db_->GetObjectStatus(handler, object_identifier, &object_status);
        callback(status, object_status == PageDbObjectStatus::TRANSIENT);
      });
}

void PageStorageImpl::GetIndexObject(
    std::unique_ptr<const Piece> piece, int64_t offset, int64_t max_size,
    Location location, fit::function<void(Status, fsl::SizedVmo)> callback) {
  ObjectDigestInfo digest_info =
      GetObjectDigestInfo(piece->GetIdentifier().object_digest());

  FXL_DCHECK(digest_info.piece_type == PieceType::INDEX);
  fxl::StringView content = piece->GetData();
  const FileIndex* file_index;
  Status status = FileIndexSerialization::ParseFileIndex(content, &file_index);
  if (status != Status::OK) {
    callback(Status::FORMAT_ERROR, nullptr);
    return;
  }

  int64_t start = GetObjectPartStart(offset, file_index->size());
  int64_t length = GetObjectPartLength(max_size, file_index->size(), start);
  zx::vmo raw_vmo;
  zx_status_t zx_status = zx::vmo::create(length, 0, &raw_vmo);
  if (zx_status != ZX_OK) {
    FXL_LOG(WARNING) << "Unable to create VMO of size: " << length;
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }
  fsl::SizedVmo vmo(std::move(raw_vmo), length);

  fsl::SizedVmo vmo_copy;
  zx_status = vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to duplicate vmo. Status: " << zx_status;
    callback(Status::INTERNAL_ERROR, nullptr);
    return;
  }

  FillBufferWithObjectContent(
      std::move(piece), std::move(vmo_copy), start, length, 0,
      file_index->size(), location,
      [vmo = std::move(vmo), callback = std::move(callback)](
          Status status) mutable { callback(status, std::move(vmo)); });
}

void PageStorageImpl::FillBufferWithObjectContent(
    ObjectIdentifier object_identifier, fsl::SizedVmo vmo,
    int64_t global_offset, int64_t global_size, int64_t current_position,
    int64_t object_size, Location location,
    fit::function<void(Status)> callback) {
  GetPiece(object_identifier, [this, object_identifier, vmo = std::move(vmo),
                               global_offset, global_size, current_position,
                               object_size, location,
                               callback = std::move(callback)](
                                  Status status,
                                  std::unique_ptr<const Piece> piece) mutable {
    if (status == Status::INTERNAL_NOT_FOUND) {
      // If we encounter a missing piece, we should download it
      // together with a subtree needed for the request.
      // Note that a similar mechanism is at play in |GetObjectPart|
      // but it triggers only when the root piece is missing; here, we
      // cover the case where the root piece was present and we started
      // recursing in the tree but eventually found a missing piece.
      if (location != Location::NETWORK) {
        callback(Status::INTERNAL_NOT_FOUND);
        return;
      }
      DownloadObjectPart(
          object_identifier, global_offset, global_size, current_position,
          [this, object_identifier, location, vmo = std::move(vmo),
           global_offset, global_size, current_position, object_size,
           callback = std::move(callback)](Status status) mutable {
            if (status != Status::OK) {
              callback(status);
              return;
            }
            FillBufferWithObjectContent(
                object_identifier, std::move(vmo), global_offset, global_size,
                current_position, object_size, location, std::move(callback));
            return;
          });
      return;
    }
    if (status != Status::OK) {
      callback(status);
      return;
    }

    FXL_DCHECK(piece);
    FillBufferWithObjectContent(std::move(piece), std::move(vmo), global_offset,
                                global_size, current_position, object_size,
                                location, std::move(callback));
  });
}

void PageStorageImpl::FillBufferWithObjectContent(
    std::unique_ptr<const Piece> piece, fsl::SizedVmo vmo,
    int64_t global_offset, int64_t global_size, int64_t current_position,
    int64_t object_size, Location location,
    fit::function<void(Status)> callback) {
  fxl::StringView content = piece->GetData();
  ObjectDigestInfo digest_info =
      GetObjectDigestInfo(piece->GetIdentifier().object_digest());
  if (digest_info.is_inlined() || digest_info.is_chunk()) {
    if (object_size != static_cast<int64_t>(content.size())) {
      FXL_LOG(ERROR) << "Error in serialization format. Expecting object: "
                     << piece->GetIdentifier()
                     << " to have size: " << object_size
                     << ", but found an object of size: " << content.size();
      callback(Status::FORMAT_ERROR);
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
        std::min(static_cast<int64_t>(content.size()) - read_offset,
                 global_size - write_offset);
    FXL_DCHECK(read_write_size > 0);
    fxl::StringView read_substr = content.substr(read_offset, read_write_size);
    zx_status_t zx_status =
        vmo.vmo().write(read_substr.data(), write_offset, read_write_size);
    if (zx_status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to write to vmo. Status: " << zx_status;
      callback(Status::INTERNAL_ERROR);
      return;
    }
    callback(Status::OK);
    return;
  }

  const FileIndex* file_index;
  Status status = FileIndexSerialization::ParseFileIndex(content, &file_index);
  if (status != Status::OK) {
    callback(Status::FORMAT_ERROR);
    return;
  }
  if (static_cast<int64_t>(file_index->size()) != object_size) {
    FXL_LOG(ERROR) << "Error in serialization format. Expecting object: "
                   << piece->GetIdentifier() << " to have size " << object_size
                   << ", but found an index object of size: "
                   << file_index->size();
    callback(Status::FORMAT_ERROR);
    return;
  }

  int64_t sub_offset = 0;
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  for (const auto* child : *file_index->children()) {
    if (sub_offset + child->size() > file_index->size()) {
      callback(Status::FORMAT_ERROR);
      return;
    }
    int64_t child_position = current_position + sub_offset;
    ObjectIdentifier child_identifier =
        ToObjectIdentifier(child->object_identifier());
    if (child_position + static_cast<int64_t>(child->size()) < global_offset) {
      sub_offset += child->size();
      continue;
    }
    if (global_offset + global_size < child_position) {
      break;
    }
    fsl::SizedVmo vmo_copy;
    zx_status_t zx_status =
        vmo.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_WRITE, &vmo_copy);
    if (zx_status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to duplicate vmo. Status: " << zx_status;
      callback(Status::INTERNAL_ERROR);
      return;
    }
    FillBufferWithObjectContent(child_identifier, std::move(vmo_copy),
                                global_offset, global_size, child_position,
                                child->size(), location, waiter->NewCallback());
    sub_offset += child->size();
  }
  waiter->Finalize(std::move(callback));
}

void PageStorageImpl::DownloadObjectPart(ObjectIdentifier object_identifier,
                                         int64_t global_offset,
                                         int64_t global_size,
                                         int64_t current_position,
                                         fit::function<void(Status)> callback) {
  if (!page_sync_) {
    callback(Status::NETWORK_ERROR);
    return;
  }
  FXL_DCHECK(
      !GetObjectDigestInfo(object_identifier.object_digest()).is_inlined());

  coroutine_manager_.StartCoroutine(
      std::move(callback),
      [this, object_identifier = std::move(object_identifier), global_offset,
       global_size,
       current_position](CoroutineHandler* handler,
                         fit::function<void(Status)> callback) mutable {
        Status status;
        ChangeSource source;
        IsObjectSynced is_object_synced;
        std::unique_ptr<DataSource::DataChunk> chunk;

        // Retrieve an object from the network.
        if (coroutine::SyncCall(
                handler,
                [this, object_identifier](
                    fit::function<void(Status, ChangeSource, IsObjectSynced,
                                       std::unique_ptr<DataSource::DataChunk>)>
                        callback) mutable {
                  page_sync_->GetObject(std::move(object_identifier),
                                        std::move(callback));
                },
                &status, &source, &is_object_synced,
                &chunk) == coroutine::ContinuationStatus::INTERRUPTED) {
          callback(Status::INTERRUPTED);
          return;
        }

        if (status != Status::OK) {
          callback(status);
          return;
        }

        auto digest_info =
            GetObjectDigestInfo(object_identifier.object_digest());
        FXL_DCHECK(!digest_info.is_inlined());

        if (object_identifier.object_digest() !=
            ComputeObjectDigest(digest_info.piece_type, digest_info.object_type,
                                chunk->Get())) {
          callback(Status::OBJECT_DIGEST_MISMATCH);
          return;
        }

        if (digest_info.is_chunk()) {
          // Downloaded chunk objects are directly added to storage. They have
          // no children at all, so references are empty.
          callback(SynchronousAddPiece(
              handler,
              DataChunkPiece(std::move(object_identifier), std::move(chunk)),
              source, is_object_synced, {}));
          return;
        }

        const FileIndex* file_index;
        // Dowloaded index objects need to be recursed into to find chunk
        // objects at the leaves.
        status =
            FileIndexSerialization::ParseFileIndex(chunk->Get(), &file_index);
        if (status != Status::OK) {
          callback(Status::FORMAT_ERROR);
          return;
        }

        if (digest_info.object_type == ObjectType::TREE_NODE) {
          // For the root node, we need to do some extra work of sanitizing the
          // user-provided offset and size (as we now know the full size of the
          // object). For non-root indexes and chunks, we assume the offset and
          // max_size are already sanitized.
          global_offset = GetObjectPartStart(global_offset, file_index->size());
          global_size = GetObjectPartLength(global_size, file_index->size(),
                                            global_offset);
        }

        int64_t sub_offset = 0;
        auto waiter =
            fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
        for (const auto* child : *file_index->children()) {
          // Recursively download the children of this index that we are
          // interested in.
          if (sub_offset + child->size() > file_index->size()) {
            callback(Status::FORMAT_ERROR);
            return;
          }
          int64_t child_position = current_position + sub_offset;
          if (global_offset + global_size < child_position) {
            // This child starts after the part we want ends: skip it and all
            // the remaining ones.
            break;
          }

          sub_offset += child->size();
          if (child_position + static_cast<int64_t>(child->size()) <
              global_offset) {
            // This child ends before the part we want starts: skip it.
            continue;
          }
          ObjectIdentifier child_identifier =
              ToObjectIdentifier(child->object_identifier());
          if (GetObjectDigestInfo(child_identifier.object_digest())
                  .is_inlined()) {
            // Nothing to download for inline pieces, but we must account for
            // their size.
            continue;
          }

          Status status = db_->HasObject(handler, child_identifier);
          // Check if the child is in the database. Otherwise, download it
          // recursively.
          if (status == Status::OK) {
            continue;
          } else if (status != Status::INTERNAL_NOT_FOUND) {
            callback(status);
            return;
          }
          DownloadObjectPart(std::move(child_identifier), global_offset,
                             global_size, child_position,
                             waiter->NewCallback());
        }

        if (coroutine::Wait(handler, std::move(waiter), &status) ==
            coroutine::ContinuationStatus::INTERRUPTED) {
          callback(Status::INTERRUPTED);
          return;
        }

        if (status != Status::OK) {
          callback(status);
          return;
        }

        // TODO(kerneis): handle children.
        callback(SynchronousAddPiece(
            handler,
            DataChunkPiece(std::move(object_identifier), std::move(chunk)),
            source, is_object_synced, {}));
      });
}

Status PageStorageImpl::SynchronousInit(CoroutineHandler* handler) {
  // Add the default page head if this page is empty.
  std::vector<std::pair<zx::time_utc, CommitId>> heads;
  Status s = db_->GetHeads(handler, &heads);
  if (s != Status::OK) {
    return s;
  }
  // Cache the heads and update the live commit tracker.
  std::vector<std::unique_ptr<const Commit>> commits;
  if (heads.empty()) {
    s = db_->AddHead(handler, kFirstPageCommitId, zx::time_utc());
    if (s != Status::OK) {
      return s;
    }
    std::unique_ptr<const Commit> head_commit;
    s = SynchronousGetCommit(handler, kFirstPageCommitId.ToString(),
                             &head_commit);
    if (s != Status::OK) {
      return s;
    }
    commits.push_back(std::move(head_commit));
  } else {
    auto waiter = fxl::MakeRefCounted<
        callback::Waiter<Status, std::unique_ptr<const Commit>>>(Status::OK);

    for (const auto& head : heads) {
      GetCommit(head.second, waiter->NewCallback());
    }
    if (coroutine::Wait(handler, std::move(waiter), &s, &commits) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    if (s != Status::OK) {
      return s;
    }
  }
  commit_tracker_.AddHeads(std::move(commits));

  // Cache whether this page is online or not.
  return db_->IsPageOnline(handler, &page_is_online_);
}

Status PageStorageImpl::SynchronousGetCommit(
    CoroutineHandler* handler, CommitId commit_id,
    std::unique_ptr<const Commit>* commit) {
  if (IsFirstCommit(commit_id)) {
    Status s;
    if (coroutine::SyncCall(
            handler,
            [this](fit::function<void(Status, std::unique_ptr<const Commit>)>
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
                               std::move(new_objects), nullptr);
}

Status PageStorageImpl::SynchronousAddCommitsFromSync(
    CoroutineHandler* handler, std::vector<CommitIdAndBytes> ids_and_bytes,
    ChangeSource source, std::vector<CommitId>* missing_ids) {
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

    if (status != Status::INTERNAL_NOT_FOUND) {
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
    btree::GetObjectsFromSync(environment_->coroutine_service(), this,
                              leaf.second->GetRootIdentifier(),
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

  return SynchronousAddCommits(handler, std::move(commits), source,
                               std::vector<ObjectIdentifier>(), missing_ids);
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
  if (coroutine::Wait(handler, std::move(waiter), &s, &result) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
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
  std::unique_ptr<PageDb::Batch> batch;
  Status status = db_->StartBatch(handler, &batch);
  if (status != Status::OK) {
    return status;
  }
  status = SynchronousMarkCommitSyncedInBatch(handler, batch.get(), commit_id);
  if (status != Status::OK) {
    return status;
  }
  return batch->Execute(handler);
}

Status PageStorageImpl::SynchronousMarkCommitSyncedInBatch(
    CoroutineHandler* handler, PageDb::Batch* batch,
    const CommitId& commit_id) {
  Status status = SynchronousMarkPageOnline(handler, batch);
  if (status != Status::OK) {
    return status;
  }
  return batch->MarkCommitIdSynced(handler, commit_id);
}

Status PageStorageImpl::SynchronousAddCommits(
    CoroutineHandler* handler,
    std::vector<std::unique_ptr<const Commit>> commits, ChangeSource source,
    std::vector<ObjectIdentifier> new_objects,
    std::vector<CommitId>* missing_ids) {
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

  std::map<CommitId, std::unique_ptr<const Commit>> heads_to_add;
  std::vector<CommitId> removed_heads;

  int orphaned_commits = 0;
  for (auto& commit : commits) {
    // We need to check if we are adding an already present remote commit here
    // because we might both download and locally commit the same commit at
    // roughly the same time. As commit writing is asynchronous, the previous
    // check in AddCommitsFromSync may have not matched any commit, while a
    // commit got added in between.
    Status s = ContainsCommit(handler, commit->GetId());
    if (s == Status::OK) {
      if (source == ChangeSource::CLOUD) {
        s = SynchronousMarkCommitSyncedInBatch(handler, batch.get(),
                                               commit->GetId());
        if (s != Status::OK) {
          return s;
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
      s = batch->AddMerge(handler, parent_ids[0], parent_ids[1],
                          commit->GetId());
      if (s != Status::OK) {
        return s;
      }
    }

    // Commits should arrive in order. Check that the parents are either
    // present in PageDb or in the list of already processed commits.
    // If the commit arrive out of order, print an error, but skip it
    // temporarily so that the Ledger can recover if all the needed commits
    // are received in a single batch.
    bool orphaned_commit = false;
    for (const CommitIdView& parent_id : parent_ids) {
      if (added_commits.find(&parent_id) == added_commits.end()) {
        s = ContainsCommit(handler, parent_id);
        if (s == Status::INTERRUPTED) {
          return s;
        }
        if (s != Status::OK) {
          FXL_LOG(ERROR) << "Failed to find parent commit \""
                         << ToHex(parent_id) << "\" of commit \""
                         << convert::ToHex(commit->GetId()) << "\".";
          if (s == Status::INTERNAL_NOT_FOUND) {
            if (missing_ids) {
              missing_ids->push_back(parent_id.ToString());
            }
            orphaned_commit = true;
            continue;
          }
          return Status::INTERNAL_ERROR;
        }
      }
      // Remove the parent from the list of heads.
      if (!heads_to_add.erase(parent_id.ToString())) {
        // parent_id was not added in the batch: remove it from heads in Db.
        s = batch->RemoveHead(handler, parent_id);
        if (s != Status::OK) {
          return s;
        }
        removed_heads.push_back(parent_id.ToString());
      }
    }

    // The commit could not be added. Skip it.
    if (orphaned_commit) {
      orphaned_commits++;
      continue;
    }

    s = batch->AddCommitStorageBytes(handler, commit->GetId(),
                                     commit->GetRootIdentifier(),
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
    heads_to_add[commit->GetId()] = commit->Clone();

    added_commits.insert(&commit->GetId());
    commits_to_send.push_back(std::move(commit));
  }

  if (orphaned_commits > 0) {
    if (source != ChangeSource::P2P) {
      ledger::ReportEvent(
          ledger::CobaltEvent::COMMITS_RECEIVED_OUT_OF_ORDER_NOT_RECOVERED);
      FXL_LOG(ERROR)
          << "Failed adding commits. Found " << orphaned_commits
          << " orphaned commits (one of their parent was not found).";
    }
    return Status::INTERNAL_NOT_FOUND;
  }

  // Update heads in Db.
  for (const auto& head : heads_to_add) {
    Status s = batch->AddHead(handler, head.second->GetId(),
                              head.second->GetTimestamp());
    if (s != Status::OK) {
      return s;
    }
  }

  // If adding local commits, mark all new pieces as local.
  Status s = MarkAllPiecesLocal(handler, batch.get(), std::move(new_objects));
  if (s != Status::OK) {
    return s;
  }

  s = batch->Execute(handler);

  if (s != Status::OK) {
    return s;
  }

  // Only update the cache of heads after a successful update of the PageDb.
  commit_tracker_.RemoveHeads(std::move(removed_heads));
  std::vector<std::unique_ptr<const Commit>> new_heads;
  std::transform(
      std::make_move_iterator(heads_to_add.begin()),
      std::make_move_iterator(heads_to_add.end()),
      std::back_inserter(new_heads),
      [](std::pair<CommitId, std::unique_ptr<const Commit>>&& head) {
        return std::move(std::get<std::unique_ptr<const Commit>>(head));
      });
  commit_tracker_.AddHeads(std::move(new_heads));
  NotifyWatchersOfNewCommits(commits_to_send, source);

  return s;
}

Status PageStorageImpl::SynchronousAddPiece(
    CoroutineHandler* handler, const Piece& piece, ChangeSource source,
    IsObjectSynced is_object_synced, ObjectReferencesAndPriority references) {
  FXL_DCHECK(
      !GetObjectDigestInfo(piece.GetIdentifier().object_digest()).is_inlined());
  FXL_DCHECK(
      piece.GetIdentifier().object_digest() ==
      ComputeObjectDigest(
          GetObjectDigestInfo(piece.GetIdentifier().object_digest()).piece_type,
          GetObjectDigestInfo(piece.GetIdentifier().object_digest())
              .object_type,
          piece.GetData()));

  Status status = db_->HasObject(handler, piece.GetIdentifier());
  if (status == Status::INTERNAL_NOT_FOUND) {
    PageDbObjectStatus object_status;
    switch (is_object_synced) {
      case IsObjectSynced::NO:
        object_status =
            (source == ChangeSource::LOCAL ? PageDbObjectStatus::TRANSIENT
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

Status PageStorageImpl::SynchronousMarkPageOnline(
    coroutine::CoroutineHandler* handler, PageDb::Batch* batch) {
  if (page_is_online_) {
    return Status::OK;
  }
  Status status = batch->MarkPageOnline(handler);
  if (status == Status::OK) {
    page_is_online_ = true;
  }
  return status;
}

FXL_WARN_UNUSED_RESULT Status
PageStorageImpl::SynchronousGetEmptyNodeIdentifier(
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
            &status,
            &object_identifier) == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    if (status != Status::OK) {
      return status;
    }
    empty_node_id_ =
        std::make_unique<ObjectIdentifier>(std::move(object_identifier));
  }
  *empty_node_id = empty_node_id_.get();
  return Status::OK;
}

}  // namespace storage
