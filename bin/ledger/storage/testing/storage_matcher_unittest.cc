// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/testing/storage_matcher.h"

#include "gtest/gtest.h"

namespace storage {
namespace {

TEST(StorageMatcher, MatchesDigest) {
  ObjectIdentifier id = {0, 1, "hello"};

  EXPECT_THAT(id, MatchesDigest("hello"));
  EXPECT_THAT(id, Not(MatchesDigest("hexllo")));
}

TEST(StorageMatcher, MatchesEntry2Parameters) {
  Entry entry = {"key", {0, 1, "hello"}, KeyPriority::EAGER};

  EXPECT_THAT(entry, MatchesEntry({"key", MatchesDigest("hello")}));
  EXPECT_THAT(entry, Not(MatchesEntry({"key", MatchesDigest("helo")})));
  EXPECT_THAT(entry, Not(MatchesEntry({"ky", MatchesDigest("hello")})));
}

TEST(StorageMatcher, MatchesEntry3Parameters) {
  Entry entry = {"key", {0, 1, "hello"}, KeyPriority::EAGER};

  EXPECT_THAT(
      entry, MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::EAGER}));
  EXPECT_THAT(
      entry,
      Not(MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::LAZY})));
}

}  // namespace
}  // namespace storage
