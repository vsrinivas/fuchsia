// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/common_ancestor.h"

#include <utility>

#include <lib/fit/function.h>

#include "lib/callback/waiter.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"

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

// Find the common ancestor the 2 given commits.
//
// The algorithm goes as follows: we keep a set of "active" commits, ordered
// by generation order. Until this set has only one element, we take the
// commit with the greater generation (the one deepest in the commit graph)
// and replace it by its parent. If we seed the initial set with two commits,
// we get their unique lowest common ancestor.
// At each step of the iteration we request the parent commits of all commits
// with the same generation.
storage::Status FindCommonAncestorSync(
    coroutine::CoroutineHandler* handler, storage::PageStorage* storage,
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    std::unique_ptr<const storage::Commit>* result) {
  std::set<std::unique_ptr<const storage::Commit>, GenerationComparator>
      commits;
  commits.emplace(std::move(head1));
  commits.emplace(std::move(head2));

  while (commits.size() > 1) {
    // Pop the newest commits and retrieve their parents.
    uint64_t expected_generation = (*commits.rbegin())->GetGeneration();
    auto waiter = fxl::MakeRefCounted<callback::Waiter<
        storage::Status, std::unique_ptr<const storage::Commit>>>(
        storage::Status::OK);
    while (commits.size() > 1 &&
           expected_generation == (*commits.rbegin())->GetGeneration()) {
      // Pop the newest commit.
      std::unique_ptr<const storage::Commit> commit =
          std::move(const_cast<std::unique_ptr<const storage::Commit>&>(
              *commits.rbegin()));
      commits.erase(std::prev(commits.end()));
      // Request its parents.
      for (const auto& parent_id : commit->GetParentIds()) {
        storage->GetCommit(parent_id, waiter->NewCallback());
      }
    }
    storage::Status status;
    std::vector<std::unique_ptr<const storage::Commit>> parents;
    if (coroutine::SyncCall(
            handler,
            [waiter](auto callback) { waiter->Finalize(std::move(callback)); },
            &status, &parents) == coroutine::ContinuationStatus::INTERRUPTED) {
      return storage::Status::INTERRUPTED;
    }
    if (status != storage::Status::OK) {
      return status;
    }
    // Once the parents have been retrieved, add these in the set.
    // ancestor in that generation.
    for (auto& parent : parents) {
      commits.insert(std::move(parent));
    }
  }
  FXL_DCHECK(commits.size() == 1);
  // TODO(qsr): Use std::set::extract when C++17 is available.
  *result = std::move(
      const_cast<std::unique_ptr<const storage::Commit>&>(*commits.begin()));
  return storage::Status::OK;
}

}  // namespace

void FindCommonAncestor(
    coroutine::CoroutineService* coroutine_service,
    storage::PageStorage* const storage,
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  coroutine_service->StartCoroutine(
      [storage, head1 = std::move(head1), head2 = std::move(head2),
       callback =
           std::move(callback)](coroutine::CoroutineHandler* handler) mutable {
        std::unique_ptr<const storage::Commit> result;
        storage::Status status = FindCommonAncestorSync(
            handler, storage, std::move(head1), std::move(head2), &result);
        callback(PageUtils::ConvertStatus(status), std::move(result));
      });
}

}  // namespace ledger
