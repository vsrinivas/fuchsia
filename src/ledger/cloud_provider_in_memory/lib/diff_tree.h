// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_DIFF_TREE_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_DIFF_TREE_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "src/ledger/bin/fidl/include/types.h"

namespace ledger {

// The state of a page is represented by the commit id of the corresponding commit, or std::nullopt
// for the empty state.
using PageState = std::optional<std::string>;

// A DiffEntry as seen by the cloud.
struct CloudDiffEntry {
  std::string entry_id;
  cloud_provider::Operation operation;
  std::string data;
};

bool operator==(const CloudDiffEntry& lhs, const CloudDiffEntry& rhs);

// The structure used to store the diffs uploaded by clients.
//
// When a new commit is added with an associated diff, it is added to the DiffTree with the base
// state of the diff as its parent. Each diff in the DiffTree is stored in a DiffTreeEntry, which
// contains the parent state, the diff, an origin and a depth relative to the origin:
//  - for a diff entry that uses a base commit for which we don't already have a diff entry, the
//  origin is the base commit and the depth is 1
//  - for a subsequent diff entry that uses a base commit for which we already have a diff entry,
//  its origin is the origin of the base commit's DiffTreeEntry and its depth is the depth of the
//  base commit's DiffTreeEntry + 1
// We consider that all states that are not present (ie.  have no associated diffs) have themselves
// as their origins, and depth 0.
//
// When we remove compatibility with non-diff Ledgers, the origin of all commits will be the empty
// page state.
// TODO(ambre): remove origin when it is not needed anymore.
class DiffTree {
 public:
  // Constructs an empty DiffTree.
  DiffTree();

  // Adds a diff defining |target_commit| to the tree, with base state |base_state| and diff
  // |entries|. It is invalid to add such a diff if |target_commit| is already present in the diff
  // tree, either as a base or as a target commit.
  void AddDiff(std::string target_commit, PageState base_state,
               std::vector<CloudDiffEntry> entries);

  // Returns the smallest diff (by number of entries) between |target_state| and one of the states
  // in |known_commit_ids|, or between |target_state| and its origin state. The diff is returned as
  // a pair of the base state and the diff to go from the base state to the target state.
  std::pair<PageState, std::vector<CloudDiffEntry>> GetSmallestDiff(
      PageState target_state, std::vector<std::string> known_commit_ids);

 private:
  // An entry in the DiffTree.
  struct DiffTreeEntry {
    // The state of the page at the parent.
    PageState parent_state;
    // The "origin" state of the page reached by following the parents in the DiffTree.
    PageState origin_state;
    // Always non-zero: the distance between this node and its origin in the DiffTree.
    size_t depth;
    // The diff entries describing the difference between this state and the parent state.
    std::vector<CloudDiffEntry> entries;
  };

  // Returns |true| and the diff between |base_state| and |target_state| in |diff| if such a diff
  // exists. If there is no path between the two states, returns |false|.
  bool GetDiff(PageState base_state, PageState target_state, std::vector<CloudDiffEntry>* diff);

  // Returns the depth and origin associated with a state.
  std::pair<size_t, PageState> GetDepthAndOrigin(PageState state);

  // A map from commit ids to the corresponding DiffTree entry. The empty page has no associated
  // diff.
  std::map<std::string, DiffTreeEntry> diffs_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_DIFF_TREE_H_
