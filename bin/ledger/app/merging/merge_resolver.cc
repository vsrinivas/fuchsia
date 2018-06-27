// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include <lib/fit/function.h>

#include "lib/callback/scoped_callback.h"
#include "lib/callback/trace_callback.h"
#include "lib/callback/waiter.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/app/merging/common_ancestor.h"
#include "peridot/bin/ledger/app/merging/ledger_merge_manager.h"
#include "peridot/bin/ledger/app/merging/merge_strategy.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"

namespace ledger {

MergeResolver::MergeResolver(fit::closure on_destroyed,
                             Environment* environment,
                             storage::PageStorage* storage,
                             std::unique_ptr<backoff::Backoff> backoff)
    : coroutine_service_(environment->coroutine_service()),
      storage_(storage),
      backoff_(std::move(backoff)),
      on_destroyed_(std::move(on_destroyed)),
      task_runner_(environment->async()) {
  storage_->AddCommitWatcher(this);
  PostCheckConflicts(DelayedStatus::DONT_DELAY);
}

MergeResolver::~MergeResolver() {
  storage_->RemoveCommitWatcher(this);
  on_destroyed_();
}

void MergeResolver::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool MergeResolver::IsEmpty() { return !merge_in_progress_; }

bool MergeResolver::HasUnfinishedMerges() {
  return merge_in_progress_ || check_conflicts_in_progress_ ||
         check_conflicts_task_count_ != 0 || in_delay_;
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
    PostCheckConflicts(DelayedStatus::DONT_DELAY);
  }
}

void MergeResolver::SetPageManager(PageManager* page_manager) {
  FXL_DCHECK(page_manager_ == nullptr);
  page_manager_ = page_manager;
}

void MergeResolver::RegisterNoConflictCallback(
    fit::function<void(ConflictResolutionWaitStatus)> callback) {
  no_conflict_callbacks_.push_back(std::move(callback));
}

void MergeResolver::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
    storage::ChangeSource source) {
  PostCheckConflicts(source == storage::ChangeSource::LOCAL
                         ? DelayedStatus::DONT_DELAY
                         // We delay remote commits.
                         : DelayedStatus::MAY_DELAY);
}

void MergeResolver::PostCheckConflicts(DelayedStatus delayed_status) {
  check_conflicts_task_count_++;
  task_runner_.PostTask([this, delayed_status] {
    check_conflicts_task_count_--;
    CheckConflicts(delayed_status);
  });
}

void MergeResolver::CheckConflicts(DelayedStatus delayed_status) {
  if (!strategy_ || merge_in_progress_ || check_conflicts_in_progress_ ||
      in_delay_) {
    // No strategy is set, or a merge is already in progress, or we are already
    // checking for conflicts, or we are delaying merges. Let's bail out early.
    return;
  }
  check_conflicts_in_progress_ = true;
  storage_->GetHeadCommitIds(task_runner_.MakeScoped(
      [this, delayed_status](storage::Status s,
                             std::vector<storage::CommitId> heads) {
        check_conflicts_in_progress_ = false;
        if (s != storage::Status::OK || heads.size() == 1) {
          // An error occurred, or there is no conflict. In either case, return
          // early.
          if (s != storage::Status::OK) {
            FXL_LOG(ERROR) << "Failed to get head commits with status " << s;
          } else {
            for (auto& callback : no_conflict_callbacks_) {
              callback(has_merged_
                           ? ConflictResolutionWaitStatus::CONFLICTS_RESOLVED
                           : ConflictResolutionWaitStatus::NO_CONFLICTS);
            }
            no_conflict_callbacks_.clear();
            has_merged_ = false;
          }
          if (on_empty_callback_) {
            on_empty_callback_();
          }
          return;
        }
        if (!strategy_) {
          if (on_empty_callback_) {
            on_empty_callback_();
          }
          return;
        }
        merge_in_progress_ = true;
        heads.resize(2);
        ResolveConflicts(delayed_status, std::move(heads));
      }));
}

void MergeResolver::ResolveConflicts(DelayedStatus delayed_status,
                                     std::vector<storage::CommitId> heads) {
  FXL_DCHECK(heads.size() == 2);
  auto cleanup =
      fxl::MakeAutoCall(task_runner_.MakeScoped([this, delayed_status] {
        // |merge_in_progress_| must be reset before calling
        // |on_empty_callback_|.
        merge_in_progress_ = false;

        if (has_next_strategy_) {
          strategy_ = std::move(next_strategy_);
          next_strategy_.reset();
          has_next_strategy_ = false;
        }
        PostCheckConflicts(delayed_status);
        // Call on_empty_callback_ at the very end as it might delete the
        // resolver.
        if (on_empty_callback_) {
          on_empty_callback_();
        }
      }));
  uint64_t id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("ledger", "merge", id);
  auto tracing =
      fxl::MakeAutoCall([id] { TRACE_ASYNC_END("ledger", "merge", id); });

  auto waiter = fxl::MakeRefCounted<callback::Waiter<
      storage::Status, std::unique_ptr<const storage::Commit>>>(
      storage::Status::OK);
  for (const storage::CommitId& id : heads) {
    storage_->GetCommit(id, waiter->NewCallback());
  }
  waiter->Finalize(TRACE_CALLBACK(
      task_runner_.MakeScoped(fxl::MakeCopyable([this, delayed_status,
                                                 cleanup = std::move(cleanup),
                                                 tracing = std::move(tracing)](
                                                    storage::Status status,
                                                    std::vector<std::unique_ptr<
                                                        const storage::Commit>>
                                                        commits) mutable {
        FXL_DCHECK(commits.size() == 2);
        FXL_DCHECK(commits[0]->GetTimestamp() <= commits[1]->GetTimestamp());

        if (commits[0]->GetParentIds().size() == 2 &&
            commits[1]->GetParentIds().size() == 2) {
          if (delayed_status == DelayedStatus::MAY_DELAY) {
            // If trying to merge 2 merge commits, add some delay with
            // exponential backoff.
            auto delay_callback = [this] {
              in_delay_ = false;
              CheckConflicts(DelayedStatus::DONT_DELAY);
            };
            in_delay_ = true;
            task_runner_.PostDelayedTask(
                TRACE_CALLBACK(std::move(delay_callback), "ledger",
                               "merge_delay"),
                backoff_->GetNext());
            cleanup.cancel();
            merge_in_progress_ = false;
            // We don't want to continue merging if nobody is interested
            // (all clients disconnected).
            if (on_empty_callback_) {
              on_empty_callback_();
            }
            return;
          }
          // If delayed_status is not intial, report the merge.
          ReportEvent(CobaltEvent::MERGED_COMMITS_MERGED);
        } else {
          // No longer merging 2 merge commits, reinitialize the
          // exponential backoff.
          backoff_->Reset();
        }

        // Check if the 2 parents have the same content.
        if (commits[0]->GetRootIdentifier() ==
            commits[1]->GetRootIdentifier()) {
          // In that case, the result must be a commit with the same
          // content.
          storage_->StartMergeCommit(
              commits[0]->GetId(), commits[1]->GetId(),
              TRACE_CALLBACK(
                  task_runner_.MakeScoped(fxl::MakeCopyable(
                      [this, cleanup = std::move(cleanup),
                       tracing = std::move(tracing)](
                          storage::Status status,
                          std::unique_ptr<storage::Journal> journal) mutable {
                        if (status != storage::Status::OK) {
                          FXL_LOG(ERROR) << "Unable to start merge commit "
                                            "for identical commits.";
                          return;
                        }
                        has_merged_ = true;
                        storage_->CommitJournal(
                            std::move(journal),
                            fxl::MakeCopyable(
                                [cleanup = std::move(cleanup),
                                 tracing = std::move(tracing)](
                                    storage::Status status,
                                    std::unique_ptr<const storage::Commit>) {
                                  if (status != storage::Status::OK) {
                                    FXL_LOG(ERROR) << "Unable to merge "
                                                      "identical commits.";
                                    return;
                                  }

                                  // Report the merge.
                                  ReportEvent(CobaltEvent::COMMITS_MERGED);
                                }));
                      })),
                  "ledger", "merge_same_commit_journal"));
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

        // Merge the first two commits using the most recent one as the
        // base.
        auto head1 = std::move(commits[0]);
        auto head2 = std::move(commits[1]);
        FindCommonAncestor(
            coroutine_service_, storage_, head1->Clone(), head2->Clone(),
            TRACE_CALLBACK(
                task_runner_.MakeScoped(fxl::MakeCopyable(
                    [this, head1 = std::move(head1), head2 = std::move(head2),
                     cleanup = std::move(cleanup),
                     tracing = std::move(tracing)](
                        Status status, std::unique_ptr<const storage::Commit>
                                           common_ancestor) mutable {
                      // If the strategy has been changed, bail
                      // early.
                      if (has_next_strategy_) {
                        return;
                      }

                      if (status != Status::OK) {
                        FXL_LOG(ERROR) << "Failed to find common ancestor "
                                          "of head commits.";
                        return;
                      }
                      auto strategy_callback = fxl::MakeCopyable(
                          [cleanup = std::move(cleanup),
                           tracing = std::move(tracing)](Status status) {
                            if (status != Status::OK) {
                              FXL_LOG(WARNING) << "Merging failed. "
                                                  "Will try again "
                                                  "later.";
                              return;
                            }
                            ReportEvent(CobaltEvent::COMMITS_MERGED);
                          });
                      has_merged_ = true;
                      strategy_->Merge(
                          storage_, page_manager_, std::move(head1),
                          std::move(head2), std::move(common_ancestor),
                          TRACE_CALLBACK(std::move(strategy_callback), "ledger",
                                         "merge_strategy_merge"));
                    })),
                "ledger", "merge_find_common_ancestor"));
      })),
      "ledger", "merge_get_commit_finalize"));
}

}  // namespace ledger
