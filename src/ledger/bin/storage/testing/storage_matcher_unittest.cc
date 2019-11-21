// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/storage_matcher.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/testing/id_and_parent_ids_commit.h"
#include "src/ledger/lib/convert/convert.h"

namespace storage {
namespace {

TEST(StorageMatcher, MatchesDigest) {
  ObjectIdentifier id = ObjectIdentifier(0, ObjectDigest("hello"), nullptr);
  EXPECT_THAT(id, MatchesDigest("hello"));
  EXPECT_THAT(id, Not(MatchesDigest("hexllo")));

  ObjectDigest digest = ObjectDigest("hello");
  EXPECT_THAT(id, MatchesDigest(digest));
}

TEST(StorageMatcher, MatchesEntry2Parameters) {
  ObjectIdentifier id = ObjectIdentifier(0, ObjectDigest("hello"), nullptr);
  Entry entry = {"key", id, KeyPriority::EAGER, EntryId("id")};

  EXPECT_THAT(entry, MatchesEntry({"key", MatchesDigest("hello")}));
  EXPECT_THAT(entry, MatchesEntry({"key", id}));
  EXPECT_THAT(entry, Not(MatchesEntry({"key", MatchesDigest("helo")})));
  EXPECT_THAT(entry, Not(MatchesEntry({"ky", MatchesDigest("hello")})));
}

TEST(StorageMatcher, MatchesEntry3Parameters) {
  Entry entry = {"key", ObjectIdentifier(0, ObjectDigest("hello"), nullptr), KeyPriority::EAGER,
                 EntryId("id")};

  EXPECT_THAT(entry, MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::EAGER}));
  EXPECT_THAT(entry, Not(MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::LAZY})));
}

TEST(StorageMatcher, MatchesCommit) {
  CommitId zero = convert::ToString(kFirstPageCommitId);
  CommitId one = CommitId("00000000000000000000000000000001", kCommitIdSize);
  CommitId two = CommitId("00000000000000000000000000000002", kCommitIdSize);
  CommitId three = CommitId("00000000000000000000000000000003", kCommitIdSize);
  CommitId four = CommitId("00000000000000000000000000000004", kCommitIdSize);
  CommitId five = CommitId("00000000000000000000000000000005", kCommitIdSize);
  IdAndParentIdsCommit commit = IdAndParentIdsCommit(zero, {one, two, three});

  EXPECT_THAT(commit, MatchesCommit(zero, {one, two, three}));
  EXPECT_THAT(commit, Not(MatchesCommit(five, {one, two, three})));
  EXPECT_THAT(commit, Not(MatchesCommit(zero, {})));
  EXPECT_THAT(commit, Not(MatchesCommit(zero, {one, two})));
  EXPECT_THAT(commit, Not(MatchesCommit(zero, {one, two, three, four})));
}

}  // namespace
}  // namespace storage
