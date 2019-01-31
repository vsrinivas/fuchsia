// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include <lib/callback/scoped_callback.h>
#include <lib/callback/trace_callback.h>
#include <lib/callback/waiter.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/app/merging/common_ancestor.h"
#include "peridot/bin/ledger/app/merging/ledger_merge_manager.h"
#include "peridot/bin/ledger/app/merging/merge_strategy.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"

namespace ledger {

// Enumerates merge candidates' indexes among current head commits.
class MergeResolver::MergeCandidates {
 public:
  MergeCandidates();

  // Resets the MergeCandidates and sets the total number of head commits to
  // |head_count|.
  void ResetCandidates(size_t head_count);

  // Returns whether MergeCandidates should be reset. A reset is necessary
  // when the head commits have changed, i.e. when there is a successful merge
  // or on a new commit.
  bool NeedsReset() { return needs_reset_; }

  // Returns the current pair on indexes of head commits to be merged.
  std::pair<size_t, size_t> GetCurrentPair();

  // Returns whether there is a merge candidate pair available.
  bool HasCandidate();

  // Returns true if there was a network error in one of the previous merge
  // attempts. This does not include merges before |ResetCandidates| was
  // called.
  bool HadNetworkErrors() { return had_network_errors_; }

  // Should be called after a successful merge.
  void OnMergeSuccess();

  // Should be called after an unsuccessful merge.
  void OnMergeError(Status status);

  // Should be called when new commits are available.
  void OnNewCommits();

  // Returns the number of head commits.
  size_t head_count() { return head_count_; }

 private:
  // Advances to the next available pair of merge candidates.
  void PrepareNext();

  size_t head_count_;
  std::pair<size_t, size_t> current_pair_;
  bool needs_reset_ = true;
  bool had_network_errors_ = false;
};

MergeResolver::MergeCandidates::MergeCandidates() {}

void MergeResolver::MergeCandidates::ResetCandidates(size_t head_count) {
  head_count_ = head_count;
  current_pair_ = {0, 1};
  needs_reset_ = false;
  had_network_errors_ = false;
}

bool MergeResolver::MergeCandidates::HasCandidate() {
  return current_pair_.first != head_count_ - 1;
}

std::pair<size_t, size_t> MergeResolver::MergeCandidates::GetCurrentPair() {
  return current_pair_;
}

void MergeResolver::MergeCandidates::OnMergeSuccess() { needs_reset_ = true; }

void MergeResolver::MergeCandidates::OnMergeError(Status status) {
  if (status == Status::NETWORK_ERROR) {
    // The contents of the common ancestor are unavailable locally and it wasn't
    // possible to retrieve them through the network: Ignore this pair of heads
    // for now.
    had_network_errors_ = true;
    PrepareNext();
  } else {
    FXL_LOG(WARNING) << "Merging failed. Will try again later.";
  }
}

void MergeResolver::MergeCandidates::OnNewCommits() { needs_reset_ = true; }

void MergeResolver::MergeCandidates::PrepareNext() {
  ++current_pair_.second;
  if (current_pair_.second == head_count_) {
    ++current_pair_.first;
    current_pair_.second = current_pair_.first + 1;
  }
}

MergeResolver::MergeResolver(fit::closure on_destroyed,
                             Environment* environment,
                             storage::PageStorage* storage,
                             std::unique_ptr<backoff::Backoff> backoff)
    : coroutine_service_(environment->coroutine_service()),
      storage_(storage),
      backoff_(std::move(backoff)),
      merge_candidates_(std::make_unique<MergeCandidates>()),
      on_destroyed_(std::move(on_destroyed)),
      task_runner_(environment->dispatcher()) {
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
         check_conflicts_task_count_ != 0 || in_delay_ ||
         merge_candidates_->HadNetworkErrors();
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
  merge_candidates_->OnNewCommits();
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

        if (merge_candidates_->NeedsReset()) {
          merge_candidates_->ResetCandidates(heads.size());
        }
        FXL_DCHECK(merge_candidates_->head_count() == heads.size())
            << merge_candidates_->head_count() << " != " << heads.size();

        if (s != storage::Status::OK || heads.size() == 1 ||
            !(merge_candidates_->HasCandidate())) {
          // An error occurred, or there is no conflict we can resolve. In
          // either case, return early.
          if (s != storage::Status::OK) {
            FXL_LOG(ERROR) << "Failed to get head commits with status " << s;
          } else if (heads.size() == 1) {
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
        std::pair<size_t, size_t> head_indexes =
            merge_candidates_->GetCurrentPair();
        ResolveConflicts(delayed_status, std::move(heads[head_indexes.first]),
                         std::move(heads[head_indexes.second]));
      }));
}

void MergeResolver::ResolveConflicts(DelayedStatus delayed_status,
                                     storage::CommitId head1,
                                     storage::CommitId head2) {
  auto cleanup = fit::defer(task_runner_.MakeScoped([this, delayed_status] {
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
  auto tracing = fit::defer([id] { TRACE_ASYNC_END("ledger", "merge", id); });

  auto waiter = fxl::MakeRefCounted<callback::Waiter<
      storage::Status, std::unique_ptr<const storage::Commit>>>(
      storage::Status::OK);
  storage_->GetCommit(head1, waiter->NewCallback());
  storage_->GetCommit(head2, waiter->NewCallback());
  waiter->Finalize(TRACE_CALLBACK(
      task_runner_.MakeScoped(
          [this, delayed_status, cleanup = std::move(cleanup),
           tracing = std::move(tracing)](
              storage::Status status,
              std::vector<std::unique_ptr<const storage::Commit>>
                  commits) mutable {
            if (status != storage::Status::OK) {
              FXL_LOG(ERROR)
                  << "Failed to retrieve head commits. Status: " << status;
              return;
            }
            FXL_DCHECK(commits.size() == 2);
            FXL_DCHECK(commits[0]->GetTimestamp() <=
                       commits[1]->GetTimestamp());

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
              // If delayed_status is not initial, report the merge.
              ReportEvent(CobaltEvent::MERGED_COMMITS_MERGED);
            } else {
              // No longer merging 2 merge commits, reinitialize the exponential
              // backoff.
              backoff_->Reset();
            }

            // Check if the 2 parents have the same content.
            if (commits[0]->GetRootIdentifier() ==
                commits[1]->GetRootIdentifier()) {
              // In that case, the result must be a commit with the same
              // content.
              MergeCommitsWithSameContent(
                  std::move(commits[0]), std::move(commits[1]),
                  [cleanup = std::move(cleanup), tracing = std::move(tracing)] {
                    // Report the merge.
                    ReportEvent(CobaltEvent::COMMITS_MERGED);
                  });
              return;
            }

            // If the strategy has been changed, bail early.
            if (has_next_strategy_) {
              return;
            }

            // Merge the first two commits using the most recent one as the
            // base.
            FindCommonAncestorAndMerge(
                std::move(commits[0]), std::move(commits[1]),
                [cleanup = std::move(cleanup), tracing = std::move(tracing)] {
                  ReportEvent(CobaltEvent::COMMITS_MERGED);
                });
          }),
      "ledger", "merge_get_commit_finalize"));
}

void MergeResolver::MergeCommitsWithSameContent(
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    fit::closure on_successful_merge) {
  storage_->StartMergeCommit(
      head1->GetId(), head2->GetId(),
      TRACE_CALLBACK(
          task_runner_.MakeScoped(
              [this, on_successful_merge = std::move(on_successful_merge)](
                  storage::Status status,
                  std::unique_ptr<storage::Journal> journal) mutable {
                if (status != storage::Status::OK) {
                  FXL_LOG(ERROR)
                      << "Unable to start merge commit for identical commits.";
                  return;
                }
                has_merged_ = true;
                storage_->CommitJournal(
                    std::move(journal),
                    [on_successful_merge = std::move(on_successful_merge)](
                        storage::Status status,
                        std::unique_ptr<const storage::Commit>) {
                      if (status != storage::Status::OK) {
                        FXL_LOG(ERROR) << "Unable to merge identical commits.";
                        return;
                      }
                      on_successful_merge();
                    });
              }),
          "ledger", "merge_same_commit_journal"));
}

void MergeResolver::FindCommonAncestorAndMerge(
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    fit::closure on_successful_merge) {
  FindCommonAncestor(
      coroutine_service_, storage_, head1->Clone(), head2->Clone(),
      TRACE_CALLBACK(
          task_runner_.MakeScoped(
              [this, head1 = std::move(head1), head2 = std::move(head2),
               on_successful_merge = std::move(on_successful_merge)](
                  Status status, std::unique_ptr<const storage::Commit>
                                     common_ancestor) mutable {
                // If the strategy has been changed, bail early.
                if (has_next_strategy_) {
                  return;
                }

                if (status != Status::OK) {
                  FXL_LOG(ERROR)
                      << "Failed to find common ancestor of head commits.";
                  return;
                }
                auto strategy_callback =
                    [this, on_successful_merge =
                               std::move(on_successful_merge)](Status status) {
                      if (status != Status::OK) {
                        merge_candidates_->OnMergeError(status);
                        return;
                      }
                      merge_candidates_->OnMergeSuccess();
                      on_successful_merge();
                    };
                has_merged_ = true;
                strategy_->Merge(
                    storage_, page_manager_, std::move(head1), std::move(head2),
                    std::move(common_ancestor),
                    TRACE_CALLBACK(std::move(strategy_callback), "ledger",
                                   "merge_strategy_merge"));
              }),
          "ledger", "merge_find_common_ancestor"));
}

}  // namespace ledger
