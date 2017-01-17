// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/custom_merge_strategy.h"

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
class CustomMergeStrategy::CustomMerger {
 public:
  CustomMerger(storage::PageStorage* storage,
               PageManager* page_manager,
               ConflictResolver* conflict_resolver,
               std::unique_ptr<const storage::Commit> left,
               std::unique_ptr<const storage::Commit> right,
               std::unique_ptr<const storage::Commit> ancestor,
               ftl::Closure on_done);
  ~CustomMerger();

  void Start();
  void Cancel();
  void Done();

 private:
  void OnChangesReady(storage::Status status,
                      std::vector<PageChangePtr> changes);
  void OnMergeDone(fidl::Array<MergedValuePtr> merged_values);

  storage::PageStorage* const storage_;
  PageManager* const manager_;
  ConflictResolver* const conflict_resolver_;

  std::unique_ptr<const storage::Commit> const left_;
  std::unique_ptr<const storage::Commit> const right_;
  std::unique_ptr<const storage::Commit> const ancestor_;

  ftl::Closure on_done_;

  std::unique_ptr<storage::Journal> journal_;
  bool cancelled_ = false;

  // This must be the last member of the class.
  ftl::WeakPtrFactory<CustomMergeStrategy::CustomMerger> weak_factory_;
};

CustomMergeStrategy::CustomMerger::CustomMerger(
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

CustomMergeStrategy::CustomMerger::~CustomMerger() {
  if (journal_) {
    journal_->Rollback();
  }
}

void CustomMergeStrategy::CustomMerger::Start() {
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

void CustomMergeStrategy::CustomMerger::OnChangesReady(
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

  PageSnapshotPtr page_snapshot;
  manager_->BindPageSnapshot(ancestor_->Clone(), page_snapshot.NewRequest());
  conflict_resolver_->Resolve(
      std::move(changes[0]), std::move(changes[1]),
      std::move(page_snapshot), [weak_this = weak_factory_.GetWeakPtr()](
                                    fidl::Array<MergedValuePtr> merged_values) {
        if (!weak_this) {
          return;
        }
        weak_this->OnMergeDone(std::move(merged_values));
      });
}

void CustomMergeStrategy::CustomMerger::OnMergeDone(
    fidl::Array<MergedValuePtr> merged_values) {
  if (cancelled_) {
    Done();
    return;
  }

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
    weak_this->journal_->Commit([weak_this](
        storage::Status status, const storage::CommitId& commit_id) {
      if (status != storage::Status::OK) {
        FTL_LOG(ERROR) << "Unable to commit merge journal: " << status;
      }
      if (weak_this) {
        weak_this->Done();
      }
    });
  }));
}

void CustomMergeStrategy::CustomMerger::Cancel() {
  cancelled_ = true;
  if (journal_) {
    journal_->Rollback();
    journal_.reset();
  }
}

void CustomMergeStrategy::CustomMerger::Done() {
  auto on_done = std::move(on_done_);
  on_done_ = nullptr;
  on_done();
}

CustomMergeStrategy::CustomMergeStrategy(ConflictResolverPtr conflict_resolver)
    : conflict_resolver_(std::move(conflict_resolver)) {
  conflict_resolver_.set_connection_error_handler([this]() {
    // If a merge is in progress, it must be forcefully terminated.
    if (in_progress_merge_) {
      auto in_progress_merge = std::move(in_progress_merge_);
      in_progress_merge_.reset();
      in_progress_merge->Done();
    }
    if (on_error_) {
      on_error_();
    }
  });
}

CustomMergeStrategy::~CustomMergeStrategy() {}

void CustomMergeStrategy::SetOnError(ftl::Closure on_error) {
  on_error_ = on_error;
}

void CustomMergeStrategy::Merge(storage::PageStorage* storage,
                                PageManager* page_manager,
                                std::unique_ptr<const storage::Commit> head_1,
                                std::unique_ptr<const storage::Commit> head_2,
                                std::unique_ptr<const storage::Commit> ancestor,
                                ftl::Closure on_done) {
  FTL_DCHECK(head_1->GetTimestamp() <= head_2->GetTimestamp());
  FTL_DCHECK(!in_progress_merge_);

  // Both merger and this CustomMergeStrategy instance are owned by the same
  // MergeResolver object. MergeResolver makes sure that |Merger|s are cancelled
  // before destroying the CustomMergeStrategy merge strategy.
  in_progress_merge_ = std::make_unique<CustomMergeStrategy::CustomMerger>(
      storage, page_manager, &*conflict_resolver_, std::move(head_2),
      std::move(head_1), std::move(ancestor),
      [ this, on_done = std::move(on_done) ] {
        in_progress_merge_.reset();
        on_done();
      });

  in_progress_merge_->Start();
}

void CustomMergeStrategy::Cancel() {
  FTL_DCHECK(in_progress_merge_);
  in_progress_merge_->Cancel();
}

}  // namespace ledger
