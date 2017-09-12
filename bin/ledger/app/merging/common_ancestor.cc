// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/common_ancestor.h"

#include <utility>

#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"

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
    const fxl::RefPtr<fxl::TaskRunner> task_runner,
    storage::PageStorage* storage,
    std::set<std::unique_ptr<const storage::Commit>, GenerationComparator>*
        commits,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  FXL_DCHECK(!commits->empty());
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
  waiter->Finalize([
    task_runner, storage, commits, callback = std::move(callback)
  ](storage::Status status,
    std::vector<std::unique_ptr<const storage::Commit>> parents) mutable {
    if (status != storage::Status::OK) {
      callback(PageUtils::ConvertStatus(status), nullptr);
      return;
    }
    // Push the parents in the commit set.
    for (auto& parent : parents) {
      commits->insert(std::move(parent));
    }
    task_runner->PostTask([
      task_runner, storage, commits, callback = std::move(callback)
    ]() mutable {
      FindCommonAncestorInGeneration(task_runner, storage, commits,
                                     std::move(callback));
    });
  });
}

}  // namespace

void FindCommonAncestor(
    const fxl::RefPtr<fxl::TaskRunner> task_runner,
    storage::PageStorage* const storage,
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
      task_runner, storage, commits.get(), fxl::MakeCopyable([
        commits = std::move(commits), callback = std::move(callback)
      ](Status status, std::unique_ptr<const storage::Commit> ancestor) {
        callback(status, std::move(ancestor));
      }));
}

}  // namespace ledger
