// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/piece_tracker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

using PieceTokenTest = ledger::TestWithEnvironment;

ObjectIdentifier CreateObjectIdentifier(ObjectDigest digest) { return {1u, 2u, std::move(digest)}; }

TEST_F(PieceTokenTest, PieceTracker) {
  const ObjectIdentifier identifier = RandomObjectIdentifier(environment_.random());
  const ObjectIdentifier another_identifier = RandomObjectIdentifier(environment_.random());

  PieceTracker tracker;
  EXPECT_EQ(tracker.count(identifier), 0);
  EXPECT_EQ(tracker.size(), 0);

  auto token_1 = tracker.GetPieceToken(identifier);
  EXPECT_EQ(tracker.count(identifier), 1);
  EXPECT_EQ(tracker.size(), 1);

  auto token_2 = tracker.GetPieceToken(identifier);
  EXPECT_NE(token_1.get(), token_2.get());
  EXPECT_EQ(tracker.count(identifier), 2);
  EXPECT_EQ(tracker.size(), 1);

  auto token_3 = tracker.GetPieceToken(another_identifier);
  EXPECT_EQ(tracker.count(identifier), 2);
  EXPECT_EQ(tracker.count(another_identifier), 1);
  EXPECT_EQ(tracker.size(), 2);

  token_1.reset();
  EXPECT_EQ(tracker.count(identifier), 1);
  EXPECT_EQ(tracker.count(another_identifier), 1);
  EXPECT_EQ(tracker.size(), 2);

  token_2.reset();
  EXPECT_EQ(tracker.count(identifier), 0);
  EXPECT_EQ(tracker.count(another_identifier), 1);
  EXPECT_EQ(tracker.size(), 1);

  token_3.reset();
  EXPECT_EQ(tracker.count(identifier), 0);
  EXPECT_EQ(tracker.count(another_identifier), 0);
  EXPECT_EQ(tracker.size(), 0);
}

TEST_F(PieceTokenTest, DiscardableToken) {
  std::string data = RandomString(environment_.random(), 12);
  ObjectIdentifier identifier =
      CreateObjectIdentifier(ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, data));

  const DiscardableToken token(identifier);
  EXPECT_EQ(token.GetIdentifier(), identifier);
}

}  // namespace
}  // namespace storage
