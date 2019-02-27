// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/commit_pack/commit_pack.h"

#include <gtest/gtest.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/convert/convert.h"
#include "sdk/fidl/fuchsia.ledger.cloud/serialized_commits_generated.h"

namespace cloud_provider {
namespace {

using CommitPackTest = ::testing::TestWithParam<std::vector<CommitPackEntry>>;

TEST_P(CommitPackTest, BackAndForth) {
  const std::vector<CommitPackEntry> commits = GetParam();
  CommitPack commit_pack;
  ASSERT_TRUE(EncodeCommitPack(commits, &commit_pack));
  std::vector<CommitPackEntry> result;
  ASSERT_TRUE(DecodeCommitPack(commit_pack, &result));
  EXPECT_EQ(commits, result);
}

INSTANTIATE_TEST_SUITE_P(CommitPackTest, CommitPackTest,
                         ::testing::Values(std::vector<CommitPackEntry>(),
                                           std::vector<CommitPackEntry>{
                                               {"id_0", "data_0"},
                                               {"id_1", "data_1"}}));

TEST_F(CommitPackTest, NullPack) {
  // Encode SerializedCommits with the field |commits| left null.
  flatbuffers::FlatBufferBuilder builder;
  SerializedCommitsBuilder commits_builder(builder);
  CommitPack commit_pack;
  flatbuffers::Offset<SerializedCommits> off = commits_builder.Finish();
  builder.Finish(off);
  fsl::VmoFromString(convert::ToStringView(builder), &commit_pack.buffer);

  // Should be rejected.
  std::vector<CommitPackEntry> result;
  EXPECT_FALSE(DecodeCommitPack(commit_pack, &result));
}

TEST_F(CommitPackTest, NullFields) {
  // Encode a SerializedCommit with all null fields.
  flatbuffers::FlatBufferBuilder builder;
  SerializedCommitBuilder commit_builder(builder);
  CommitPack commit_pack;
  flatbuffers::Offset<SerializedCommit> off = commit_builder.Finish();
  auto entries_offsets = builder.CreateVector(
      std::vector<flatbuffers::Offset<SerializedCommit>>({off}));
  builder.Finish(CreateSerializedCommits(builder, entries_offsets));
  fsl::VmoFromString(convert::ToStringView(builder), &commit_pack.buffer);

  // Should be rejected.
  std::vector<CommitPackEntry> result;
  EXPECT_FALSE(DecodeCommitPack(commit_pack, &result));
}

}  // namespace
}  // namespace cloud_provider
