// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <set>

#include "apps/ledger/src/app/merging/ledger_merge_manager.h"
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {
// Comparator for commits that order commits based on their generation, then on
// their id.
struct GenerationComparator {
  bool operator()(const std::unique_ptr<const storage::Commit>& lhs,
                  const std::unique_ptr<const storage::Commit>& rhs) const {
    uint64_t lhs_generation = lhs->GetGeneration();
    uint64_t rhs_generation = rhs->GetGeneration();
    return lhs_generation == rhs_generation ? lhs->GetId() < rhs->GetId()
                                            : lhs_generation < rhs_generation;
  }
};

// Recursively retrieves the parents of the most recent commits in the given
// set, i.e. the commits with the highest generation. The recursion stops when
// only one commit is left in the set, which is the lowest common ancestor.
void FindCommonAncestorInGeneration(
    storage::PageStorage* storage,
    std::set<std::unique_ptr<const storage::Commit>, GenerationComparator>*
        commits,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  FTL_DCHECK(!commits->empty());
  // If there is only one commit in the set it is the lowest common ancestor.
  if (commits->size() == 1) {
    callback(Status::OK,
             std::move(const_cast<std::unique_ptr<const storage::Commit>&>(
                 *commits->rbegin())));
    return;
  }
  // Pop the newest commits and retrieve their parents.
  uint64_t expected_generation = (*commits->rbegin())->GetGeneration();
  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Commit>>::Create(
          storage::Status::OK);
  while (commits->size() > 1 &&
         expected_generation == (*commits->rbegin())->GetGeneration()) {
    // Pop the newest commit.
    std::unique_ptr<const storage::Commit> commit =
        std::move(const_cast<std::unique_ptr<const storage::Commit>&>(
            *commits->rbegin()));
    auto it = commits->end();
    --it;
    commits->erase(it);
    // Request its parents.
    for (const auto& parent_id : commit->GetParentIds()) {
      storage->GetCommit(parent_id, waiter->NewCallback());
    }
  }
  // Once the parents have been retrieved, recursively try to find the common
  // ancestor in that generation.
  waiter->Finalize(
      ftl::MakeCopyable([ storage, commits, callback = std::move(callback) ](
          storage::Status status,
          std::vector<std::unique_ptr<const storage::Commit>> parents) mutable {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status), nullptr);
          return;
        }
        // Push the parents in the commit set.
        for (auto& parent : parents) {
          commits->insert(std::move(parent));
        }
        FindCommonAncestorInGeneration(storage, commits, std::move(callback));
      }));
}

// Sets merge_in_progress value to true on construction and false on
// destruction. It should be used to ensure that merge_in_progress_ is not
// accidently left to be true after a early operation exit.
class MergeInProgress {
 public:
  MergeInProgress(bool* merge_in_progress)
      : merge_in_progress_(merge_in_progress) {
    *merge_in_progress_ = true;
  }

  MergeInProgress(MergeInProgress&& other)
      : merge_in_progress_(other.merge_in_progress_) {
    other.merge_in_progress_ = nullptr;
  }

  ~MergeInProgress() {
    if (merge_in_progress_) {
      *merge_in_progress_ = false;
    }
  }

 private:
  bool* merge_in_progress_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MergeInProgress);
};

}  // namespace

MergeResolver::MergeResolver(ftl::Closure on_destroyed,
                             storage::PageStorage* storage)
    : storage_(storage), on_destroyed_(on_destroyed), weak_ptr_factory_(this) {
  storage_->AddCommitWatcher(this);
  PostCheckConflicts();
}

MergeResolver::~MergeResolver() {
  storage_->RemoveCommitWatcher(this);
  on_destroyed_();
}

void MergeResolver::set_on_empty(ftl::Closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool MergeResolver::IsEmpty() {
  return !merge_in_progress_;
}

void MergeResolver::SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy) {
  if (merge_in_progress_) {
    FTL_DCHECK(strategy_);
    next_strategy_ = std::move(strategy);
    switch_strategy_ = true;
    strategy_->Cancel();
    return;
  }
  strategy_.swap(strategy);
  if (strategy_) {
    PostCheckConflicts();
  }
}

void MergeResolver::SetPageManager(PageManager* page_manager) {
  FTL_DCHECK(page_manager_ == nullptr);
  page_manager_ = page_manager;
}

void MergeResolver::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& commits,
    storage::ChangeSource source) {
  PostCheckConflicts();
}

void MergeResolver::PostCheckConflicts() {
  mtl::MessageLoop::GetCurrent()
      ->task_runner()
      ->PostTask([weak_this_ptr = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak_this_ptr) {
          weak_this_ptr->CheckConflicts();
        }
      });
}
void MergeResolver::CheckConflicts() {
  if (!strategy_ || merge_in_progress_) {
    // No strategy, or a merge already in progress. Let's bail out early.
    return;
  }

  std::vector<storage::CommitId> heads;
  storage::Status s = storage_->GetHeadCommitIds(&heads);
  FTL_DCHECK(s == storage::Status::OK);
  if (heads.size() == 1) {
    // No conflict.
    return;
  }
  ResolveConflicts(std::move(heads));
}

void MergeResolver::ResolveConflicts(std::vector<storage::CommitId> heads) {
  FTL_DCHECK(heads.size() >= 2);
  MergeInProgress merge_token(&merge_in_progress_);
  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Commit>>::Create(
          storage::Status::OK);
  for (const storage::CommitId& id : heads) {
    storage_->GetCommit(id, waiter->NewCallback());
  }
  waiter->Finalize(ftl::MakeCopyable([
    this, merge_token = std::move(merge_token)
  ](storage::Status status,
    std::vector<std::unique_ptr<const storage::Commit>> commits) mutable {
    if (status != storage::Status::OK) {
      FTL_LOG(ERROR) << "Failed to retrieve head commits.";
      return;
    }
    FTL_DCHECK(commits.size() >= 2);
    std::sort(commits.begin(), commits.end(),
              [](const std::unique_ptr<const storage::Commit>& lhs,
                 const std::unique_ptr<const storage::Commit>& rhs) {
                return lhs->GetTimestamp() < rhs->GetTimestamp();
              });

    // Merge the first two commits using the most recent one as the base.
    auto head1 = std::move(commits[0]);
    auto head2 = std::move(commits[1]);
    FindCommonAncestor(
        head1->Clone(), head2->Clone(),
        ftl::MakeCopyable([
          this, head1 = std::move(head1), head2 = std::move(head2),
          merge_token = std::move(merge_token)
        ](Status status,
          std::unique_ptr<const storage::Commit> common_ancestor) mutable {
          if (status != Status::OK) {
            FTL_LOG(ERROR) << "Failed to find common ancestor of head commits.";
            return;
          }
          strategy_->Merge(
              storage_, page_manager_, std::move(head1), std::move(head2),
              std::move(common_ancestor),
              ftl::MakeCopyable(
                  [ this, merge_token = std::move(merge_token) ]() {
                    merge_in_progress_ = false;
                    if (switch_strategy_) {
                      strategy_ = std::move(next_strategy_);
                      next_strategy_.reset();
                      switch_strategy_ = false;
                    }
                    PostCheckConflicts();
                    // Call on_empty_callback_ at the very end as this
                    // might delete this.
                    if (on_empty_callback_) {
                      on_empty_callback_();
                    }
                  }));
        }));
  }));
}

void MergeResolver::FindCommonAncestor(
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  // The algorithm goes as follows: we keep a set of "active" commits, ordered
  // by generation order. Until this set has only one element, we take the
  // commit with the greater generation (the one deepest in the commit graph)
  // and replace it by its parent. If we seed the initial set with two commits,
  // we get their unique lowest common ancestor.
  // At each step of the recursion (FindCommonAncestorInGeneration) we request
  // the parent commits of all commits with the same generation.

  // commits set should not be deleted before the callback is executed.
  auto commits = std::make_unique<
      std::set<std::unique_ptr<const storage::Commit>, GenerationComparator>>();

  commits->emplace(std::move(head1));
  commits->emplace(std::move(head2));
  FindCommonAncestorInGeneration(
      storage_, commits.get(), ftl::MakeCopyable([
        commits = std::move(commits), callback = std::move(callback)
      ](Status status, std::unique_ptr<const storage::Commit> ancestor) {
        callback(status, std::move(ancestor));
      }));
}

}  // namespace ledger
