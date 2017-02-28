// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/conflict_resolver_client.h"

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/app/diff_utils.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/socket/strings.h"

namespace ledger {
ConflictResolverClient::ConflictResolverClient(
    storage::PageStorage* storage,
    PageManager* page_manager,
    ConflictResolver* conflict_resolver,
    std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right,
    std::unique_ptr<const storage::Commit> ancestor,
    ftl::Closure on_done)
    : storage_(storage),
      manager_(page_manager),
      conflict_resolver_(conflict_resolver),
      left_(std::move(left)),
      right_(std::move(right)),
      ancestor_(std::move(ancestor)),
      on_done_(std::move(on_done)),
      weak_factory_(this) {
  FTL_DCHECK(on_done_);
}

ConflictResolverClient::~ConflictResolverClient() {
  if (journal_) {
    journal_->Rollback();
  }
}

void ConflictResolverClient::Start() {
  ftl::RefPtr<callback::Waiter<storage::Status, PageChangePtr>> waiter =
      callback::Waiter<storage::Status, PageChangePtr>::Create(
          storage::Status::OK);

  diff_utils::ComputePageChange(storage_, *ancestor_, *left_,
                                waiter->NewCallback());
  diff_utils::ComputePageChange(storage_, *ancestor_, *right_,
                                waiter->NewCallback());

  waiter->Finalize([weak_this = weak_factory_.GetWeakPtr()](
      storage::Status status, std::vector<PageChangePtr> page_changes) mutable {
    if (!weak_this) {
      return;
    }
    weak_this->OnChangesReady(std::move(status), std::move(page_changes));
  });
}

void ConflictResolverClient::OnChangesReady(
    storage::Status status,
    std::vector<PageChangePtr> changes) {
  if (cancelled_) {
    Done();
    return;
  }

  if (status != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to compute diff due to error " << status
                   << ", aborting.";
    Done();
    return;
  }

  FTL_DCHECK(changes.size() == 2);

  PageSnapshotPtr page_snapshot_ancestor;
  manager_->BindPageSnapshot(ancestor_->Clone(),
                             page_snapshot_ancestor.NewRequest());

  PageSnapshotPtr page_snapshot_left;
  manager_->BindPageSnapshot(left_->Clone(), page_snapshot_left.NewRequest());

  PageSnapshotPtr page_snapshot_right;
  manager_->BindPageSnapshot(right_->Clone(), page_snapshot_right.NewRequest());

  in_client_request_ = true;
  conflict_resolver_->Resolve(
      std::move(page_snapshot_left), std::move(changes[0]),
      std::move(page_snapshot_right), std::move(changes[1]),
      std::move(page_snapshot_ancestor),
      [weak_this = weak_factory_.GetWeakPtr()](
          fidl::Array<MergedValuePtr> merged_values) {
        if (!weak_this) {
          return;
        }
        weak_this->OnMergeDone(std::move(merged_values));
      });
}

void ConflictResolverClient::OnMergeDone(
    fidl::Array<MergedValuePtr> merged_values) {
  in_client_request_ = false;
  FTL_DCHECK(!cancelled_);

  storage::Status s =
      storage_->StartMergeCommit(left_->GetId(), right_->GetId(), &journal_);
  if (s != storage::Status::OK) {
    FTL_LOG(ERROR) << "Unable to start merge commit: " << s;
    Done();
    return;
  }

  ftl::RefPtr<callback::Waiter<storage::Status, storage::ObjectId>> waiter =
      callback::Waiter<storage::Status, storage::ObjectId>::Create(
          storage::Status::OK);
  for (const MergedValuePtr& merged_value : merged_values) {
    switch (merged_value->source) {
      case ValueSource::RIGHT: {
        std::string key = convert::ToString(merged_value->key);
        storage_->GetEntryFromCommit(*right_, key, [
          key, callback = waiter->NewCallback()
        ](storage::Status status, storage::Entry entry) {
          if (status != storage::Status::OK) {
            if (status == storage::Status::NOT_FOUND) {
              FTL_LOG(ERROR)
                  << "Key " << key
                  << " is not present in the right change. Unable to proceed";
            }
            callback(status, storage::ObjectId());
            return;
          }
          callback(storage::Status::OK, entry.object_id);
        });
        break;
      }
      case ValueSource::NEW: {
        if (merged_value->new_value->is_bytes()) {
          // TODO(etiennej): Use asynchronous write, otherwise the run loop will
          // block until the socket is drained.
          mx::socket socket = mtl::WriteStringToSocket(
              convert::ToStringView(merged_value->new_value->get_bytes()));
          storage_->AddObjectFromLocal(
              std::move(socket), merged_value->new_value->get_bytes().size(),
              ftl::MakeCopyable([callback = waiter->NewCallback()](
                  storage::Status status, storage::ObjectId object_id) {
                callback(status, std::move(object_id));
              }));
        } else {
          waiter->NewCallback()(
              storage::Status::OK,
              convert::ToString(
                  merged_value->new_value->get_reference()->opaque_id));
        }
      } break;
      case ValueSource::DELETE: {
        journal_->Delete(merged_value->key);
        waiter->NewCallback()(storage::Status::OK, storage::ObjectId());
      } break;
    }
  }

  waiter->Finalize(ftl::MakeCopyable([
    weak_this = weak_factory_.GetWeakPtr(),
    merged_values = std::move(merged_values)
  ](storage::Status status, std::vector<storage::ObjectId> object_ids) {
    if (!weak_this) {
      return;
    }
    if (weak_this->cancelled_ || status != storage::Status::OK) {
      // An eventual error was logged before, no need to do it again here.
      weak_this->Done();
      return;
    }

    for (size_t i = 0; i < object_ids.size(); ++i) {
      if (object_ids[i].empty()) {
        continue;
      }
      weak_this->journal_->Put(merged_values[i]->key, object_ids[i],
                               merged_values[i]->priority == Priority::EAGER
                                   ? storage::KeyPriority::EAGER
                                   : storage::KeyPriority::LAZY);
    }
    weak_this->journal_->Commit(
        [weak_this](storage::Status status,
                    std::unique_ptr<const storage::Commit>) {
          if (status != storage::Status::OK) {
            FTL_LOG(ERROR) << "Unable to commit merge journal: " << status;
          }
          if (weak_this) {
            weak_this->journal_.reset();
            weak_this->Done();
          }
        });
  }));
}

void ConflictResolverClient::Cancel() {
  cancelled_ = true;
  if (in_client_request_) {
    Done();
  }
}

void ConflictResolverClient::Done() {
  if (journal_) {
    journal_->Rollback();
    journal_.reset();
  }
  auto on_done = std::move(on_done_);
  on_done_ = nullptr;
  on_done();
}

}  // namespace ledger
