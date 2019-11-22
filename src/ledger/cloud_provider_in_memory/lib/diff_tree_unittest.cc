// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/diff_tree.h"

#include <fuchsia/ledger/cloud/cpp/fidl.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/fidl/include/types.h"

using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace ledger {
namespace {

TEST(DiffTreeTest, SmallestDiff) {
  DiffTree tree;
  tree.AddDiff("commit1", std::nullopt,
               {{"entry0", cloud_provider::Operation::INSERTION, "data0"}});
  tree.AddDiff("commit2", "commit1", {{"entry1", cloud_provider::Operation::INSERTION, "data1_A"}});
  tree.AddDiff("commit3", "commit2", {{"entry1", cloud_provider::Operation::DELETION, "data1_B"}});

  // There are two possible diffs:
  //  - from the empty page to commit1, with one entry.
  //  - from commit3 to commit1, with zero entries.
  // Check that we select the smallest diff.
  auto [base_state, diff] = tree.GetSmallestDiff("commit1", {"commit3"});
  EXPECT_EQ(base_state, "commit3");
  EXPECT_THAT(diff, IsEmpty());
}

TEST(DiffTreeTest, SmallestDiffFromEmpty) {
  DiffTree tree;
  tree.AddDiff("commit1", std::nullopt,
               {{"entry0", cloud_provider::Operation::INSERTION, "data0"}});
  tree.AddDiff("commit2", "commit1",
               {{"entry0", cloud_provider::Operation::DELETION, "data0"},
                {"entry1", cloud_provider::Operation::INSERTION, "data1_A"}});

  // There are two possible diffs:
  //  - from the empty page to commit1, with one entry.
  //  - from commit2 to commit1, with two entries.
  // Check that we select the smallest diff.
  auto [base_state, diff] = tree.GetSmallestDiff("commit1", {"commit3"});
  EXPECT_EQ(base_state, std::nullopt);
  EXPECT_EQ(
      diff,
      (std::vector<CloudDiffEntry>{{"entry0", cloud_provider::Operation::INSERTION, "data0"}}));
}

TEST(DiffTreeTest, ComplexDiff) {
  //  The diff tree is the following:
  //     (origin)
  //        | (size = 4)
  //      (ancestor)
  //       /      \  (sizes = 2, one common deletion)
  //     (A)      (B)
  //      |        |  (sizes = 1)
  //     (C)       (D)
  // If we ask for (D) with (C) as a possible base, we should get the diff from (C) to (D).
  DiffTree tree;
  tree.AddDiff("ancestor", "origin",
               {{"e0", cloud_provider::Operation::INSERTION, "data_e0"},
                {"e1", cloud_provider::Operation::DELETION, "data_e1"},
                {"e2", cloud_provider::Operation::INSERTION, "data_e2"},
                {"e3", cloud_provider::Operation::INSERTION, "data_e3"}});
  tree.AddDiff("A", "ancestor",
               {{"e0", cloud_provider::Operation::DELETION, "data_e0"},
                {"f0", cloud_provider::Operation::INSERTION, "data_f0"}});
  tree.AddDiff("B", "ancestor",
               {{"e0", cloud_provider::Operation::DELETION, "data_e0"},
                {"g0", cloud_provider::Operation::INSERTION, "data_g0"}});
  tree.AddDiff("C", "A", {{"f1", cloud_provider::Operation::INSERTION, "data_f1"}});
  tree.AddDiff("D", "B", {{"g1", cloud_provider::Operation::INSERTION, "data_g1"}});

  // We can get a diff from the origin.
  {
    auto [base_state, diff] = tree.GetSmallestDiff("D", {});
    EXPECT_EQ(base_state, "origin");
    EXPECT_THAT(diff, UnorderedElementsAre(
                          CloudDiffEntry{"e1", cloud_provider::Operation::DELETION, "data_e1"},
                          CloudDiffEntry{"e2", cloud_provider::Operation::INSERTION, "data_e2"},
                          CloudDiffEntry{"e3", cloud_provider::Operation::INSERTION, "data_e3"},
                          CloudDiffEntry{"g0", cloud_provider::Operation::INSERTION, "data_g0"},
                          CloudDiffEntry{"g1", cloud_provider::Operation::INSERTION, "data_g1"}));
  }

  // If we have C, we can get a smaller diff.
  {
    auto [base_state, diff] = tree.GetSmallestDiff("D", {"C"});
    EXPECT_EQ(base_state, "C");
    EXPECT_THAT(diff, UnorderedElementsAre(
                          CloudDiffEntry{"f0", cloud_provider::Operation::DELETION, "data_f0"},
                          CloudDiffEntry{"f1", cloud_provider::Operation::DELETION, "data_f1"},
                          CloudDiffEntry{"g0", cloud_provider::Operation::INSERTION, "data_g0"},
                          CloudDiffEntry{"g1", cloud_provider::Operation::INSERTION, "data_g1"}));
  }
}

}  // namespace
}  // namespace ledger
