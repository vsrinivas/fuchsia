// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/diff_tree.h"

#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {
cloud_provider::Operation InvertDiffOperation(cloud_provider::Operation operation) {
  switch (operation) {
    case cloud_provider::Operation::INSERTION:
      return cloud_provider::Operation::DELETION;
    case cloud_provider::Operation::DELETION:
      return cloud_provider::Operation::INSERTION;
  };
}

CloudDiffEntry InvertDiffEntry(CloudDiffEntry input) {
  input.operation = InvertDiffOperation(input.operation);
  return input;
}

struct CompareEntryIds {
  bool operator()(const CloudDiffEntry& lhs, const CloudDiffEntry& rhs) const {
    return lhs.entry_id < rhs.entry_id;
  };
};

// For convenience, allows to iterate on containers in reverse.
template <typename T>
class Reversed {
 public:
  Reversed(const T& container) : container_(container){};
  auto begin() const { return container_.rbegin(); }
  auto end() const { return container_.rend(); }

 private:
  const T& container_;
};

// Accumulates entries in a diff and returns them compacted.
class DiffAccumulator {
 public:
  // Creates an empty accumulator.
  DiffAccumulator() = default;

  // Adds an entry to the accumulator.
  void AddEntry(CloudDiffEntry entry);

  // Returns a compacted diff equivalent to the sequence of entries given to |AddEntry|. This
  // invalidates the accumulator.
  std::vector<CloudDiffEntry> ExtractEntries();

 private:
  std::set<CloudDiffEntry, CompareEntryIds> entries_;
};

void DiffAccumulator::AddEntry(CloudDiffEntry entry) {
  cloud_provider::Operation added_operation = entry.operation;
  auto [it, inserted] = entries_.emplace(std::move(entry));
  if (!inserted) {
    // There should never be two entries with the same entry id in a state. We are either
    // cancelling an insertion with a deletion, or a deletion with an insertion.
    LEDGER_DCHECK(added_operation != it->operation) << "Double insertion or deletion";
    entries_.erase(it);
  }
}

std::vector<CloudDiffEntry> DiffAccumulator::ExtractEntries() {
  std::vector<CloudDiffEntry> output;
  output.reserve(entries_.size());
  std::move(entries_.begin(), entries_.end(), std::back_inserter(output));
  return output;
}
}  // namespace

bool operator==(const CloudDiffEntry& lhs, const CloudDiffEntry& rhs) {
  return std::tie(lhs.entry_id, lhs.operation, lhs.data) ==
         std::tie(rhs.entry_id, rhs.operation, rhs.data);
}

DiffTree::DiffTree() = default;

void DiffTree::AddDiff(std::string target_commit, PageState base_state,
                       std::vector<CloudDiffEntry> entries) {
  // Is the parent state known?
  auto [parent_depth, parent_origin] = GetDepthAndOrigin(base_state);
  DiffTreeEntry entry = {std::move(base_state), parent_origin, 1 + parent_depth,
                         std::move(entries)};
  bool inserted = diffs_.emplace(std::move(target_commit), std::move(entry)).second;
  LEDGER_DCHECK(inserted) << "Only one diff can be added for a given commit";
}

bool DiffTree::GetDiff(PageState left_state, PageState right_state,
                       std::vector<CloudDiffEntry>* diff) {
  // When computing the diff between two states, we need to go up to their common diff ancestor in
  // the DiffTree (if it exists). To make common ancestor computations easier, we precompute two
  // pieces of information:
  //  - An `origin`: this is the page state obtained by following diff bases until we reach a
  //    state that has no associated diff.
  //  - A `depth`: this is the number of diffs on the path from this state to the origin.
  //
  // Given two states A and B, if they have different origins, they have no common diff ancestor
  // in the DiffTree. If they have the same origin, we can define the "ancestor at depth X of A"
  // as the (unique) commit of depth X that is on the path from A to its origin (this is easily
  // computed from the ancestor at depth X+1). Then, the closest common ancestor of A and B is
  // obtained by finding the highest X such that the ancestor at depth X of A is the ancestor at
  // depth X of B.

  // |left_diff| and |right_diff| store the sequence of diffs encountered going from the left/right
  // states to their common ancestor.
  std::vector<const std::vector<CloudDiffEntry>*> left_diff;
  std::vector<const std::vector<CloudDiffEntry>*> right_diff;

  // Advance to the parent of the deepest of |left_state| and |right_state| until we end up at the
  // same commit. This terminates before both reach the origin.
  while (left_state != right_state) {
    // TODO(ambre): remove when we don't need compatibility with non-diff sync.
    auto [left_depth, left_origin] = GetDepthAndOrigin(left_state);
    auto [right_depth, right_origin] = GetDepthAndOrigin(right_state);
    if (left_origin != right_origin) {
      return false;
    }
    // If the left and right depth are zero, the left and right states are their own origins, and
    // are equal.
    LEDGER_DCHECK(left_depth > 0 || right_depth > 0);

    if (left_depth >= right_depth) {
      LEDGER_DCHECK(left_state);
      // The entry exists because the depth is non-zero.
      const DiffTreeEntry& left_entry = diffs_[*left_state];
      left_diff.push_back(&left_entry.entries);
      left_state = left_entry.parent_state;
    } else {
      LEDGER_DCHECK(right_state);
      // The entry exists because the depth is non-zero.
      const DiffTreeEntry& right_entry = diffs_[*right_state];
      right_diff.push_back(&right_entry.entries);
      right_state = right_entry.parent_state;
    }
  }

  // Build the final diff:
  //  - left_diff is applied in order, with its entries reversed and inverted.
  //  - right_diff is applied reversed, with its entries ordered.
  // We follow the arrows from left to right in this diagram:
  //    (left)           (right)
  //      ^                 ^
  //      | left_diff[0]    | right_diff[0]
  //    (...)             (...)
  //      ^                 ^
  //      | left_diff[n1]   | right_diff[n2]
  //     (----- ancestor -----)

  DiffAccumulator accumulator;
  for (auto diff : left_diff) {
    for (const auto& entry : Reversed(*diff)) {
      accumulator.AddEntry(InvertDiffEntry(entry));
    }
  }
  for (auto diff : Reversed(right_diff)) {
    for (const auto& entry : *diff) {
      accumulator.AddEntry(entry);
    }
  }
  *diff = accumulator.ExtractEntries();
  return true;
}

std::pair<PageState, std::vector<CloudDiffEntry>> DiffTree::GetSmallestDiff(
    PageState target_state, std::vector<std::string> known_commit_ids) {
  // Try from the origin state.
  PageState base_state = GetDepthAndOrigin(target_state).second;
  std::vector<CloudDiffEntry> smallest_diff;
  // This always succeeds: by definition there is a path from a state to its origin.
  bool has_diff_from_origin = GetDiff(base_state, target_state, &smallest_diff);
  LEDGER_DCHECK(has_diff_from_origin);

  // Try from the known commits.
  for (auto state : known_commit_ids) {
    std::vector<CloudDiffEntry> diff;
    if (GetDiff(state, target_state, &diff) && smallest_diff.size() > diff.size()) {
      base_state = state;
      smallest_diff = std::move(diff);
    }
  }

  return {base_state, smallest_diff};
}

std::pair<size_t, PageState> DiffTree::GetDepthAndOrigin(PageState state) {
  if (!state) {
    return {0, std::nullopt};
  }
  auto it = diffs_.find(*state);
  if (it == diffs_.end()) {
    // TODO(ambre): remove when we don't need compatibility with non-diff sync.
    return {0, state};
  }
  return {it->second.depth, it->second.origin_state};
}

}  // namespace ledger
