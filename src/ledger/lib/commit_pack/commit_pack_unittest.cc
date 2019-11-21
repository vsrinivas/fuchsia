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

// Checks that encoding then decoding a fidl object of type T results in the same value.
// This should be true for any object that does not contain handles.
template <typename T>
void EncodeDecodeCycle(const T& val) {
  T input = fidl::Clone(val);

  fuchsia::mem::Buffer buffer;
  ASSERT_TRUE(EncodeToBuffer(&input, &buffer));

  T output;
  ASSERT_TRUE(DecodeFromBuffer(buffer, &output));

  EXPECT_TRUE(fidl::Equals(val, output));
}

TEST(CommitPackTest, EmptyCommit) { EncodeDecodeCycle(fuchsia::ledger::cloud::Commit()); }

TEST(CommitPackTest, PositionToken) {
  EncodeDecodeCycle(fuchsia::ledger::cloud::PositionToken{convert::ToArray("hello world")});
}

TEST(CommitPackTest, Commit) {
  EncodeDecodeCycle(fuchsia::ledger::cloud::Commit()
                        .set_id(convert::ToArray("commit1"))
                        .set_data(convert::ToArray("abcdef")));
}

}  // namespace
}  // namespace cloud_provider
