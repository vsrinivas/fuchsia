// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/app/merging/common_ancestor.h"
#include "peridot/bin/ledger/app/merging/ledger_merge_manager.h"
#include "peridot/bin/ledger/app/merging/merge_strategy.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/callback/scoped_callback.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"

namespace ledger {

MergeResolver::MergeResolver(fxl::Closure on_destroyed,
                             Environment* environment,
                             storage::PageStorage* storage,
                             std::unique_ptr<backoff::Backoff> backoff)
    : storage_(storage),
      backoff_(std::move(backoff)),
      on_destroyed_(std::move(on_destroyed)),
      task_runner_(environment->main_runner()) {
  storage_->AddCommitWatcher(this);
  PostCheckConflicts();
}

MergeResolver::~MergeResolver() {
  storage_->RemoveCommitWatcher(this);
  on_destroyed_();
}

void MergeResolver::set_on_empty(fxl::Closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool MergeResolver::IsEmpty() {
  return !merge_in_progress_;
}

void MergeResolver::SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy) {
  if (merge_in_progress_) {
    FXL_DCHECK(strategy_);
    // The new strategy can be the empty strategy (nullptr), so we need a
    // separate boolean to know if we have a pending strategy change to make.
    has_next_strategy_ = true;
    next_strategy_ = std::move(strategy);
    strategy_->Cancel();
    return;
  }
  strategy_.swap(strategy);
  if (strategy_) {
    PostCheckConflicts();
  }
}

void MergeResolver::SetPageManager(PageManager* page_manager) {
  FXL_DCHECK(page_manager_ == nullptr);
  page_manager_ = page_manager;
}

void MergeResolver::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
    storage::ChangeSource /*source*/) {
  PostCheckConflicts();
}

void MergeResolver::PostCheckConflicts() {
  task_runner_.PostTask([this] { CheckConflicts(DelayedStatus::INITIAL); });
}

void MergeResolver::CheckConflicts(DelayedStatus delayed_status) {
  if (!strategy_ || merge_in_progress_) {
    // No strategy, or a merge already in progress. Let's bail out early.
    return;
  }
  merge_in_progress_ = true;
  storage_->GetHeadCommitIds(
      [this, delayed_status](storage::Status s,
                             std::vector<storage::CommitId> heads) {
        FXL_DCHECK(s == storage::Status::OK);
        if (heads.size() == 1) {
          // No conflict.
          merge_in_progress_ = false;
          return;
        }
        heads.resize(2);
        ResolveConflicts(delayed_status, std::move(heads));
      });
}

void MergeResolver::ResolveConflicts(DelayedStatus delayed_status,
                                     std::vector<storage::CommitId> heads) {
  FXL_DCHECK(heads.size() == 2);

  auto cleanup = fxl::MakeAutoCall(task_runner_.MakeScoped([this] {
    // |merge_in_progress_| must be reset before calling |on_empty_callback_|.
    merge_in_progress_ = false;

    if (has_next_strategy_) {
      strategy_ = std::move(next_strategy_);
      next_strategy_.reset();
      has_next_strategy_ = false;
    }
    PostCheckConflicts();
    // Call on_empty_callback_ at the very end as this might delete this.
    if (on_empty_callback_) {
      on_empty_callback_();
    }
  }));

  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Commit>>::Create(
          storage::Status::OK);
  for (const storage::CommitId& id : heads) {
    storage_->GetCommit(id, waiter->NewCallback());
  }
  waiter->Finalize(fxl::MakeCopyable([
    this, delayed_status, cleanup = std::move(cleanup)
  ](storage::Status status,
    std::vector<std::unique_ptr<const storage::Commit>> commits) mutable {
    FXL_DCHECK(commits.size() == 2);
    FXL_DCHECK(commits[0]->GetTimestamp() <= commits[1]->GetTimestamp());

    if (commits[0]->GetParentIds().size() == 2 &&
        commits[1]->GetParentIds().size() == 2 &&
        commits[0]->GetRootId() == commits[1]->GetRootId()) {
      if (delayed_status == DelayedStatus::INITIAL) {
        // If trying to merge 2 merge commits, add some delay with exponential
        // backoff.
        task_runner_.PostDelayedTask(
            [this] { CheckConflicts(DelayedStatus::DELAYED); },
            backoff_->GetNext());
        cleanup.cancel();
        merge_in_progress_ = false;
        // We don't want to continue merging if nobody is interested (all
        // clients disconnected).
        if (on_empty_callback_) {
          on_empty_callback_();
        }
        return;
      }
      // If delayed_status is not intial, report the merge.
      ReportEvent(CobaltEvent::MERGED_COMMITS_MERGED);
    } else {
      // No longer merging 2 merge commits, reinitialize the exponential
      // backoff.
      backoff_->Reset();
    }

    // Check if the 2 parents have the same content.
    if (commits[0]->GetRootId() == commits[1]->GetRootId()) {
      // In that case, the result must be a commit with the same content.
      storage_->StartMergeCommit(
          commits[0]->GetId(), commits[1]->GetId(),
          fxl::MakeCopyable([ this, cleanup = std::move(cleanup) ](
              storage::Status status,
              std::unique_ptr<storage::Journal> journal) mutable {
            storage_->CommitJournal(
                std::move(journal),
                fxl::MakeCopyable([cleanup = std::move(cleanup)](
                    storage::Status status,
                    std::unique_ptr<const storage::Commit>) {
                  if (status != storage::Status::OK) {
                    FXL_LOG(ERROR) << "Unable to merge identical commits.";
                    return;
                  }

                  // Report the merge.
                  ReportEvent(CobaltEvent::COMMITS_MERGED);
                }));
          }));
      return;
    }

    // If the strategy has been changed, bail early.
    if (has_next_strategy_) {
      return;
    }

    if (status != storage::Status::OK) {
      FXL_LOG(ERROR) << "Failed to retrieve head commits.";
      return;
    }

    // Merge the first two commits using the most recent one as the base.
    auto head1 = std::move(commits[0]);
    auto head2 = std::move(commits[1]);
    FindCommonAncestor(
        storage_, head1->Clone(), head2->Clone(),
        task_runner_.MakeScoped(fxl::MakeCopyable([
          this, head1 = std::move(head1), head2 = std::move(head2),
          cleanup = std::move(cleanup)
        ](Status status,
          std::unique_ptr<const storage::Commit> common_ancestor) mutable {
          // If the strategy has been changed, bail early.
          if (has_next_strategy_) {
            return;
          }

          if (status != Status::OK) {
            FXL_LOG(ERROR) << "Failed to find common ancestor of head commits.";
            return;
          }
          strategy_->Merge(
              storage_, page_manager_, std::move(head1), std::move(head2),
              std::move(common_ancestor),
              fxl::MakeCopyable([cleanup = std::move(cleanup)](Status status) {
                if (status != Status::OK) {
                  FXL_LOG(WARNING) << "Merging failed. Will try again later.";
                  return;
                }
                ReportEvent(CobaltEvent::COMMITS_MERGED);
              }));
        })));
  }));
}

}  // namespace ledger
