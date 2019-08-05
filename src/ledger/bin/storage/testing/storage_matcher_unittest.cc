// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/storage_matcher.h"

#include "gtest/gtest.h"

namespace storage {
namespace {

TEST(StorageMatcher, MatchesDigest) {
  ObjectIdentifier id = ObjectIdentifier(0, 1, ObjectDigest("hello"), nullptr);
  EXPECT_THAT(id, MatchesDigest("hello"));
  EXPECT_THAT(id, Not(MatchesDigest("hexllo")));

  ObjectDigest digest = ObjectDigest("hello");
  EXPECT_THAT(id, MatchesDigest(digest));
}

TEST(StorageMatcher, MatchesEntry2Parameters) {
  ObjectIdentifier id = ObjectIdentifier(0, 1, ObjectDigest("hello"), nullptr);
  Entry entry = {"key", id, KeyPriority::EAGER, EntryId()};

  EXPECT_THAT(entry, MatchesEntry({"key", MatchesDigest("hello")}));
  EXPECT_THAT(entry, MatchesEntry({"key", id}));
  EXPECT_THAT(entry, Not(MatchesEntry({"key", MatchesDigest("helo")})));
  EXPECT_THAT(entry, Not(MatchesEntry({"ky", MatchesDigest("hello")})));
}

TEST(StorageMatcher, MatchesEntry3Parameters) {
  Entry entry = {"key", ObjectIdentifier(0, 1, ObjectDigest("hello"), nullptr), KeyPriority::EAGER, EntryId()};

  EXPECT_THAT(entry, MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::EAGER}));
  EXPECT_THAT(entry, Not(MatchesEntry({"key", MatchesDigest("hello"), KeyPriority::LAZY})));
}

}  // namespace
}  // namespace storage
