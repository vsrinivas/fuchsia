// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/commit_pack/commit_pack.h"

#include <gtest/gtest.h>
#include <src/ledger/lib/convert/convert.h>

namespace cloud_provider {
namespace {

using CommitPackTest = ::testing::TestWithParam<std::vector<CommitPackEntry>>;

TEST_P(CommitPackTest, BackAndForth) {
  const std::vector<CommitPackEntry> commits = GetParam();
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(commits, &commit_pack));
  std::vector<CommitPackEntry> result;
  ASSERT_TRUE(DecodeCommitPack(commit_pack, &result));
  EXPECT_EQ(result, commits);
}

INSTANTIATE_TEST_SUITE_P(
    CommitPackTest, CommitPackTest,
    ::testing::Values(std::vector<CommitPackEntry>(),
                      std::vector<CommitPackEntry>{{"id_0", "data_0"}, {"id_1", "data_1"}},
                      // This vector is too large to fit in a zx::channel message.
                      std::vector<CommitPackEntry>(10000, {"id_0", "data_0"})));

}  // namespace
}  // namespace cloud_provider
