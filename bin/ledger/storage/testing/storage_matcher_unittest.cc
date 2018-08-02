// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/testing/storage_matcher.h"

#include "gtest/gtest.h"

namespace storage {
namespace {

TEST(StorageMatcher, DigestMatches) {
  ObjectIdentifier id = {0, 1, "hello"};

  EXPECT_THAT(id, DigestMatches("hello"));
  EXPECT_THAT(id, Not(DigestMatches("hexllo")));
}

TEST(StorageMatcher, EntryMatches2Parameters) {
  Entry entry = {"key", {0, 1, "hello"}, KeyPriority::EAGER};

  EXPECT_THAT(entry, EntryMatches({"key", DigestMatches("hello")}));
  EXPECT_THAT(entry, Not(EntryMatches({"key", DigestMatches("helo")})));
  EXPECT_THAT(entry, Not(EntryMatches({"ky", DigestMatches("hello")})));
}

TEST(StorageMatcher, EntryMatches3Parameters) {
  Entry entry = {"key", {0, 1, "hello"}, KeyPriority::EAGER};

  EXPECT_THAT(
      entry, EntryMatches({"key", DigestMatches("hello"), KeyPriority::EAGER}));
  EXPECT_THAT(
      entry,
      Not(EntryMatches({"key", DigestMatches("hello"), KeyPriority::LAZY})));
}

}  // namespace
}  // namespace storage
