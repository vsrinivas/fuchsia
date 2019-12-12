// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/merge_resolver.h"

#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/merging/common_ancestor.h"
#include "src/ledger/bin/app/merging/ledger_merge_manager.h"
#include "src/ledger/bin/app/merging/merge_strategy.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/trace_callback.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

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

MergeResolver::MergeCandidates::MergeCandidates() = default;

void MergeResolver::MergeCandidates::ResetCandidates(size_t head_count) {
  head_count_ = head_count;
  current_pair_ = {0, 1};
  needs_reset_ = false;
  had_network_errors_ = false;
}

bool MergeResolver::MergeCandidates::HasCandidate() {
  return current_pair_.first != head_count_ - 1;
}

std::pair<size_t, size_t> MergeResolver::MergeCandidates::GetCurrentPair() { return current_pair_; }

void MergeResolver::MergeCandidates::OnMergeSuccess() { needs_reset_ = true; }

void MergeResolver::MergeCandidates::OnMergeError(Status status) {
  if (status == Status::NETWORK_ERROR) {
    // The contents of the common ancestor are unavailable locally and it wasn't
    // possible to retrieve them through the network: Ignore this pair of heads
    // for now.
    had_network_errors_ = true;
    PrepareNext();
  } else {
    LEDGER_LOG(WARNING) << "Merging failed. Will try again later.";
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

MergeResolver::MergeResolver(fit::closure on_destroyed, Environment* environment,
                             storage::PageStorage* storage, std::unique_ptr<Backoff> backoff)
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

void MergeResolver::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool MergeResolver::IsDiscardable() const { return !merge_in_progress_; }

bool MergeResolver::HasUnfinishedMerges() {
  return merge_in_progress_ || check_conflicts_in_progress_ || check_conflicts_task_count_ != 0 ||
         in_delay_ || merge_candidates_->HadNetworkErrors();
}

void MergeResolver::SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy) {
  if (merge_in_progress_) {
    LEDGER_DCHECK(strategy_);
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

void MergeResolver::SetActivePageManager(ActivePageManager* active_page_manager) {
  LEDGER_DCHECK(active_page_manager_ == nullptr);
  active_page_manager_ = active_page_manager;
}

void MergeResolver::RegisterNoConflictCallback(
    fit::function<void(ConflictResolutionWaitStatus)> callback) {
  no_conflict_callbacks_.push_back(std::move(callback));
}

void MergeResolver::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& /*commits*/,
    storage::ChangeSource source) {
  merge_candidates_->OnNewCommits();
  PostCheckConflicts(source == storage::ChangeSource::LOCAL ? DelayedStatus::DONT_DELAY
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
  if (!strategy_ || merge_in_progress_ || check_conflicts_in_progress_ || in_delay_) {
    // No strategy is set, or a merge is already in progress, or we are already
    // checking for conflicts, or we are delaying merges. Let's bail out early.
    return;
  }
  check_conflicts_in_progress_ = true;
  std::vector<std::unique_ptr<const storage::Commit>> heads;
  Status s = storage_->GetHeadCommits(&heads);
  check_conflicts_in_progress_ = false;

  if (merge_candidates_->NeedsReset()) {
    merge_candidates_->ResetCandidates(heads.size());
  }
  LEDGER_DCHECK(merge_candidates_->head_count() == heads.size())
      << merge_candidates_->head_count() << " != " << heads.size();

  if (s != Status::OK || heads.size() == 1 || !(merge_candidates_->HasCandidate())) {
    // An error occurred, or there is no conflict we can resolve. In
    // either case, return early.
    if (s != Status::OK) {
      LEDGER_LOG(ERROR) << "Failed to get head commits with status " << s;
    } else if (heads.size() == 1) {
      for (auto& callback : no_conflict_callbacks_) {
        callback(has_merged_ ? ConflictResolutionWaitStatus::CONFLICTS_RESOLVED
                             : ConflictResolutionWaitStatus::NO_CONFLICTS);
      }
      no_conflict_callbacks_.clear();
      has_merged_ = false;
    }
    if (on_discardable_) {
      on_discardable_();
    }
    return;
  }
  if (!strategy_) {
    if (on_discardable_) {
      on_discardable_();
    }
    return;
  }
  merge_in_progress_ = true;
  std::pair<size_t, size_t> head_indexes = merge_candidates_->GetCurrentPair();
  ResolveConflicts(delayed_status, std::move(heads[head_indexes.first]),
                   std::move(heads[head_indexes.second]));
}

void MergeResolver::ResolveConflicts(DelayedStatus delayed_status,
                                     std::unique_ptr<const storage::Commit> head1,
                                     std::unique_ptr<const storage::Commit> head2) {
  auto cleanup = fit::defer(task_runner_.MakeScoped([this, delayed_status] {
    // |merge_in_progress_| must be reset before calling
    // |on_discardable_|.
    merge_in_progress_ = false;

    if (has_next_strategy_) {
      strategy_ = std::move(next_strategy_);
      next_strategy_.reset();
      has_next_strategy_ = false;
    }
    PostCheckConflicts(delayed_status);
    // Call on_discardable_ at the very end as it might delete the
    // resolver.
    if (on_discardable_) {
      on_discardable_();
    }
  }));
  uint64_t id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("ledger", "merge", id);
  auto tracing = fit::defer([id] { TRACE_ASYNC_END("ledger", "merge", id); });

  LEDGER_DCHECK(storage::Commit::TimestampOrdered(head1, head2));

  if (head1->GetParentIds().size() == 2 && head2->GetParentIds().size() == 2) {
    if (delayed_status == DelayedStatus::MAY_DELAY) {
      // If trying to merge 2 merge commits, add some delay with
      // exponential backoff.
      auto delay_callback = [this] {
        in_delay_ = false;
        CheckConflicts(DelayedStatus::DONT_DELAY);
      };
      in_delay_ = true;
      task_runner_.PostDelayedTask(
          TRACE_CALLBACK(std::move(delay_callback), "ledger", "merge_delay"), backoff_->GetNext());
      cleanup.cancel();
      merge_in_progress_ = false;
      // We don't want to continue merging if nobody is interested
      // (all clients disconnected).
      if (on_discardable_) {
        on_discardable_();
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

  // Merge the first two commits using the most recent one as the
  // base.
  RecursiveMergeOneStep(std::move(head1), std::move(head2),
                        [cleanup = std::move(cleanup), tracing = std::move(tracing)] {
                          ReportEvent(CobaltEvent::COMMITS_MERGED);
                        });
}

void MergeResolver::RecursiveMergeOneStep(std::unique_ptr<const storage::Commit> left,
                                          std::unique_ptr<const storage::Commit> right,
                                          fit::closure on_successful_merge) {
  coroutine_service_->StartCoroutine([this, left = std::move(left), right = std::move(right),
                                      on_successful_merge = std::move(on_successful_merge)](
                                         coroutine::CoroutineHandler* handler) mutable {
    TRACE_DURATION("ledger", "recursive_merge");
    Status status = RecursiveMergeSync(handler, std::move(left), std::move(right));
    if (status == Status::INTERRUPTED) {
      return;
    }
    if (status != Status::OK) {
      LEDGER_LOG(ERROR) << "Recursive merge failed";
      return;
    }
    on_successful_merge();
  });
}

Status MergeResolver::MergeCommitsToContentOfLeftSync(
    coroutine::CoroutineHandler* handler, std::unique_ptr<const storage::Commit> left,
    std::unique_ptr<const storage::Commit> right) {
  std::unique_ptr<storage::Journal> journal =
      storage_->StartMergeCommit(std::move(left), std::move(right));
  has_merged_ = true;

  Status status;
  std::unique_ptr<const storage::Commit> commit;
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, journal = std::move(journal)](
          fit::function<void(Status status, std::unique_ptr<const storage::Commit>)>
              callback) mutable {
        storage_->CommitJournal(std::move(journal), std::move(callback));
      },
      &status, &commit);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status;
}

// Synchronously get the commit with id |commit_id|. Try |candidate| if it has
// the right id, otherwise fetch it from storage.
Status MergeResolver::GetCommitSync(coroutine::CoroutineHandler* handler,
                                    storage::CommitIdView commit_id,
                                    std::unique_ptr<const storage::Commit> candidate,
                                    std::unique_ptr<const storage::Commit>* result) {
  // Exit early if we already have the commit.
  if (candidate->GetId() == commit_id) {
    *result = std::move(candidate);
    return Status::OK;
  }

  Status status;
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, commit_id](
          fit::function<void(Status status, std::unique_ptr<const storage::Commit>)> callback) {
        storage_->GetCommit(commit_id, std::move(callback));
      },
      &status, result);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  // If the strategy has been changed, bail early.
  if (has_next_strategy_) {
    return Status::INTERRUPTED;
  }
  return status;
}

// Requests the merges of |right_commit| and any element of |left_commits|, and
// return them in |merges|.
Status MergeResolver::FindMergesSync(coroutine::CoroutineHandler* handler,
                                     const std::vector<storage::CommitId>& left_commits,
                                     storage::CommitId right_commit,
                                     std::vector<storage::CommitId>* merges) {
  auto waiter = fxl::MakeRefCounted<Waiter<Status, std::vector<storage::CommitId>>>(Status::OK);
  for (const auto& left_commit : left_commits) {
    storage_->GetMergeCommitIds(left_commit, right_commit, waiter->NewCallback());
  }
  Status status;
  std::vector<std::vector<storage::CommitId>> merge_lists;
  if (coroutine::Wait(handler, std::move(waiter), &status, &merge_lists) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  // If the strategy has been changed, bail early.
  if (has_next_strategy_) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);
  for (auto& merge_list : merge_lists) {
    merges->insert(merges->end(), std::make_move_iterator(merge_list.begin()),
                   std::make_move_iterator(merge_list.end()));
  }
  return Status::OK;
}

// Try to build a merge of all commits in |ancestors|. Either the merge already
// exists and is returned in |final_merge| or one intermediate merge is
// constructed.
Status MergeResolver::MergeSetSync(coroutine::CoroutineHandler* handler,
                                   std::vector<std::unique_ptr<const storage::Commit>> ancestors,
                                   std::unique_ptr<const storage::Commit>* final_merge) {
  LEDGER_DCHECK(!ancestors.empty());

  // Sort ancestors by timestamp. This guarantees that, when we call the merge
  // strategy, the right-hand side commit is always the most recent, and also
  // matches (as much as possible) the order in which heads would be merged.
  std::sort(ancestors.begin(), ancestors.end(), storage::Commit::TimestampOrdered);

  // Build a merge of the first N ancestors. This holds the list of available
  // merges of all the ancestors examined until now. Since merges have the
  // maximum timestamp of their parents as timestamps, all commits in this list
  // are older than the Nth ancestor, but they may have lower or higher commit
  // ids.
  std::vector<storage::CommitId> merges;
  // The first ancestor is a merge of itself.
  merges.push_back(ancestors[0]->GetId());

  for (auto it = ancestors.begin() + 1, end = ancestors.end(); it < end; it++) {
    // Request the merges of the ancestor |*it| and any element of |merges|.
    std::vector<storage::CommitId> next_merges;
    auto& next_ancestor = *it;

    RETURN_ON_ERROR(FindMergesSync(handler, merges, next_ancestor->GetId(), &next_merges));
    // If |next_merges| is empty, the merges we need are not present yet. We
    // call RecursiveMergeOneStep recursively.
    if (next_merges.empty()) {
      // Try to create the merge in a deterministic way: order merges by id.
      std::sort(merges.begin(), merges.end());

      // Get |merge[0]| from storage, or from |ancestors[0]| if they are the
      // same commit.
      std::unique_ptr<const storage::Commit> last_merge;
      RETURN_ON_ERROR(GetCommitSync(handler, merges[0], std::move(ancestors[0]), &last_merge));
      // We know that |last_merge->GetTimestamp() <=
      // next_ancestor->GetTimestamp()| but the commit id of |last_merge| may be
      // higher. In case of equality we need to reorder the calls.
      if (!storage::Commit::TimestampOrdered(last_merge, next_ancestor)) {
        LEDGER_DCHECK(last_merge->GetTimestamp() == next_ancestor->GetTimestamp());
        return RecursiveMergeSync(handler, std::move(next_ancestor), std::move(last_merge));
      }
      return RecursiveMergeSync(handler, std::move(last_merge), std::move(next_ancestor));
    }
    merges = std::move(next_merges);
  }

  LEDGER_DCHECK(!merges.empty());

  // Try to create the merge in a deterministic way: order by id.
  std::sort(merges.begin(), merges.end());
  return GetCommitSync(handler, merges[0], std::move(ancestors[0]), final_merge);
}

// Does one step of recursive merging: tries to merge |left| and |right| and
// either produces a merge commit, or calls itself recursively to merge some
// common ancestors. Assumes that |left| is older than |right| according to
// |storage::Commit::TimestampOrdered|.
Status MergeResolver::RecursiveMergeSync(coroutine::CoroutineHandler* handler,
                                         std::unique_ptr<const storage::Commit> left,
                                         std::unique_ptr<const storage::Commit> right) {
  LEDGER_DCHECK(storage::Commit::TimestampOrdered(left, right));

  CommitComparison comparison;
  std::vector<std::unique_ptr<const storage::Commit>> common_ancestors;
  {
    TRACE_DURATION("ledger", "merge_common_ancestor");
    RETURN_ON_ERROR(FindCommonAncestors(handler, storage_, left->Clone(), right->Clone(),
                                        &comparison, &common_ancestors));
  }
  // If the strategy has been changed, bail early.
  if (has_next_strategy_) {
    return Status::INTERRUPTED;
  }

  if (comparison == CommitComparison::LEFT_SUBSET_OF_RIGHT) {
    return MergeCommitsToContentOfLeftSync(handler, std::move(right), std::move(left));
  } else if (comparison == CommitComparison::RIGHT_SUBSET_OF_LEFT) {
    return MergeCommitsToContentOfLeftSync(handler, std::move(left), std::move(right));
  } else if (comparison == CommitComparison::EQUIVALENT) {
    // The commits are equivalent so we can merge to the content
    // of either of them.
    return MergeCommitsToContentOfLeftSync(handler, std::move(left), std::move(right));
  }

  LEDGER_DCHECK(!common_ancestors.empty());

  // MergeSetSync has 3 possible results:
  //  - a non-OK Status
  //  - a commit returned in merge_base
  //  - OK with an empty merge_base
  std::unique_ptr<const storage::Commit> merge_base;
  RETURN_ON_ERROR(MergeSetSync(handler, std::move(common_ancestors), &merge_base));
  if (!merge_base) {
    // A commit was made, resume when notified of it.
    return Status::OK;
  }

  has_merged_ = true;

  Status merge_status;
  auto sync_call_status = coroutine::SyncCall(
      handler,
      [this, left = std::move(left), right = std::move(right),
       merge_base = std::move(merge_base)](fit::function<void(Status)> callback) mutable {
        strategy_->Merge(storage_, active_page_manager_, std::move(left), std::move(right),
                         std::move(merge_base),
                         TRACE_CALLBACK(std::move(callback), "ledger", "merge_strategy_merge"));
      },
      &merge_status);
  if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  if (merge_status != Status::OK) {
    merge_candidates_->OnMergeError(merge_status);
    return Status::ILLEGAL_STATE;
  }
  merge_candidates_->OnMergeSuccess();
  return Status::OK;
}

}  // namespace ledger
