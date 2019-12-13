// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/common_ancestor.h"

#include <lib/fit/function.h>

#include <utility>

#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_ptr.h"

namespace ledger {

namespace {

// Flags used for the commit graph search.
// Flag kBelowCommonAncestor implies kAncestorOfLeft and kAncestorOfRight.
using WalkFlags = std::bitset<3>;
enum WalkFlag {
  kAncestorOfLeft = 0,      // ancestors of the left head
  kAncestorOfRight = 1,     // ancestors of the right head
  kBelowCommonAncestor = 2  // commits with a common ancestor as a descendant
};

// A map from commits to visit to flags. This wraps a
// std::map<std::unique_ptr<const storage::Commit>, WalkFlags> with logic to
// know whether there remain commits in the map that are not below a common
// ancestor.
class CommitWalkMap {
 public:
  // Returns the number of interesting nodes.
  size_t interesting_size() { return interesting_nodes_; }

  // Pops and returns the first commit for GenerationOrder. Precondition:
  // interesting_size() > 0.
  std::pair<std::unique_ptr<const storage::Commit>, WalkFlags> Pop() {
    LEDGER_DCHECK(interesting_size() > 0);
    auto e = map_.extract(map_.begin());
    if (IsInteresting(e.mapped())) {
      interesting_nodes_--;
    }
    return std::make_pair(std::move(e.key()), e.mapped());
  }

  // Returns the highest generation number present in the map.
  uint64_t NextGeneration() { return map_.begin()->first->GetGeneration(); }

  // Adds |commit| to the map with flags |flag|, or updates the flags of
  // |commit| to include |flag|.
  void SetFlag(std::unique_ptr<const storage::Commit> commit, WalkFlags flag) {
    auto [it, exists] = map_.emplace(std::move(commit), WalkFlags());
    if (IsInteresting(it->second)) {
      // Newly inserted nodes have no flags and are not considered interesting.
      interesting_nodes_--;
    }
    it->second |= flag;
    if (IsInteresting(it->second)) {
      interesting_nodes_++;
    }
    LEDGER_DCHECK(interesting_nodes_ <= map_.size());
  }

 private:
  // Checks if some flags make a node interesting.
  bool IsInteresting(WalkFlags flags) {
    return flags.any() && !flags.test(WalkFlag::kBelowCommonAncestor);
  }

  // The number of interesting elements in the map.
  size_t interesting_nodes_ = 0;
  // The underlying map.
  std::map<std::unique_ptr<const storage::Commit>, WalkFlags, storage::GenerationComparator> map_;
};

}  // namespace

Status FindCommonAncestors(coroutine::CoroutineHandler* handler, storage::PageStorage* storage,
                           std::unique_ptr<const storage::Commit> left,
                           std::unique_ptr<const storage::Commit> right,
                           CommitComparison* comparison,
                           std::vector<std::unique_ptr<const storage::Commit>>* ancestors) {
  LEDGER_DCHECK(ancestors->empty());

  // The merge base is found by a highest-generation-first search in the commit
  // graph starting from the two heads.  The search order guarantees that child
  // commits are visited before parents. We maintain a map from commits to be
  // explored to flags. Since the flags depend on the child commits, they are
  // correct when the node is visited.
  CommitWalkMap walk_state;
  walk_state.SetFlag(std::move(left), WalkFlags().set(WalkFlag::kAncestorOfLeft));
  walk_state.SetFlag(std::move(right), WalkFlags().set(WalkFlag::kAncestorOfRight));

  // These booleans are set when we encounter a change commit that is an
  // ancestor of left but not right, or right but not left.
  bool left_has_changes = false;
  bool right_has_changes = false;

  // Loop until we only find "BelowCommonAncestors"
  while (walk_state.interesting_size() > 0) {
    uint64_t expected_generation = walk_state.NextGeneration();
    auto waiter = MakeRefCounted<
        Waiter<Status, std::pair<std::unique_ptr<const storage::Commit>, WalkFlags>>>(Status::OK);
    while (walk_state.interesting_size() > 0 &&
           expected_generation == walk_state.NextGeneration()) {
      auto [commit, flags] = walk_state.Pop();
      bool is_merge = commit->GetParentIds().size() == 2;
      // Fetch its parents
      WalkFlags parent_flags = flags;
      if (flags.test(WalkFlag::kAncestorOfLeft) && flags.test(WalkFlag::kAncestorOfRight)) {
        // The parents of common ancestors are still common ancestors, but do
        // not need to be included in the set. We mark them as uninteresting.
        parent_flags.set(WalkFlag::kBelowCommonAncestor);
      }
      for (const auto& parent_id : commit->GetParentIds()) {
        storage->GetCommit(parent_id,
                           [callback = waiter->NewCallback(), flags = parent_flags](
                               Status status, std::unique_ptr<const storage::Commit> result) {
                             callback(status, make_pair(std::move(result), flags));
                           });
      }

      if (flags.test(WalkFlag::kBelowCommonAncestor)) {
        // Stop processing uninteresting nodes.
        continue;
      }

      if (flags.count() == 2) {
        // Push common ancestors.
        ancestors->push_back(std::move(commit));
      } else if (!is_merge) {
        // Flag change commits.
        left_has_changes |= flags.test(WalkFlag::kAncestorOfLeft);
        right_has_changes |= flags.test(WalkFlag::kAncestorOfRight);
      }
    }
    Status status;
    std::vector<std::pair<std::unique_ptr<const storage::Commit>, WalkFlags>> parents;
    if (coroutine::Wait(handler, std::move(waiter), &status, &parents) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    RETURN_ON_ERROR(status);
    // Add the parents in the map of commits to be visited.
    for (auto& [parent, flags] : parents) {
      walk_state.SetFlag(std::move(parent), flags);
    }
  }

  // Subset detection
  if (!left_has_changes && !right_has_changes) {
    *comparison = CommitComparison::EQUIVALENT;
    ancestors->clear();
  } else if (!left_has_changes) {
    *comparison = CommitComparison::LEFT_SUBSET_OF_RIGHT;
    ancestors->clear();
  } else if (!right_has_changes) {
    *comparison = CommitComparison::RIGHT_SUBSET_OF_LEFT;
    ancestors->clear();
  } else {
    *comparison = CommitComparison::UNORDERED;
  }

  return Status::OK;
}

}  // namespace ledger
