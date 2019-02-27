// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/concatenate.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using ::testing::Not;
using ::testing::StartsWith;

using DbSerialization = ::ledger::TestWithEnvironment;

TEST_F(DbSerialization, MergeRow) {
  const CommitId commit1 = RandomCommitId(environment_.random());
  const CommitId commit2 = RandomCommitId(environment_.random());
  const CommitId commit3 = RandomCommitId(environment_.random());
  EXPECT_THAT(MergeRow::GetKeyFor(commit1, commit2, commit3),
              StartsWith(MergeRow::GetEntriesPrefixFor(commit2, commit3)));
}

TEST_F(DbSerialization, ReferenceRow) {
  const ObjectDigest source = RandomObjectDigest(environment_.random());
  const ObjectDigest destination = RandomObjectDigest(environment_.random());
  const CommitId commit = RandomCommitId(environment_.random());
  // Eager object
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
      StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
      StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
      StartsWith(ReferenceRow::GetEagerKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
      Not(StartsWith(ReferenceRow::GetCommitKeyPrefixFor(destination))));
  // Lazy object
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
      StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
      StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
      StartsWith(ReferenceRow::GetLazyKeyPrefixFor(destination)));
  // Commit
  EXPECT_THAT(ReferenceRow::GetKeyForCommit(commit, destination),
              StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForCommit(commit, destination),
              StartsWith(ReferenceRow::GetCommitKeyPrefixFor(destination)));
  EXPECT_THAT(
      ReferenceRow::GetKeyForCommit(commit, destination),
      Not(StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination))));
}

TEST_F(DbSerialization, ReferenceRowNoCollision) {
  // This test needs to rely on the implementation details of ReferencRow. Its
  // purpose is to fails if we remove the length byte before destination in the
  // encoding, and there is unfortunately no generic way to construct such a
  // collision.
  const ObjectDigest object1 = MakeObjectDigest("");
  const ObjectDigest object2 = MakeObjectDigest(
      fxl::Concatenate({ReferenceRow::kObjectPrefix, ReferenceRow::kEagerPrefix,
                        object1.Serialize()}));
  EXPECT_EQ(
      fxl::Concatenate({object1.Serialize(), ReferenceRow::kObjectPrefix,
                        ReferenceRow::kEagerPrefix, object2.Serialize()}),
      fxl::Concatenate({object2.Serialize(), ReferenceRow::kObjectPrefix,
                        ReferenceRow::kEagerPrefix, object1.Serialize()}));
  EXPECT_NE(
      ReferenceRow::GetKeyForObject(object1, object2, KeyPriority::EAGER),
      ReferenceRow::GetKeyForObject(object2, object1, KeyPriority::EAGER));
}

}  // namespace
}  // namespace storage
