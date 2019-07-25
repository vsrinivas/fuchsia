// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace storage {
namespace {

using ::testing::Not;
using ::testing::StartsWith;

using DbSerialization = ::ledger::TestWithEnvironment;

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) { return std::string(str, size); }

// This test makes sure nothing has changed in the rows serialization. If this
// breaks, it means action needs to be taken to avoid breaking backward
// compatibility.
TEST_F(DbSerialization, SerializationVersionControl) {
  // Head row.
  EXPECT_EQ(HeadRow::GetKeyFor("head"), "heads/head");

  // Merge row.
  EXPECT_EQ(MergeRow::GetKeyFor("parent1", "parent2", "merge"), "merges/parent1/parent2/merge");

  // Commit row.
  EXPECT_EQ(CommitRow::GetKeyFor("commit"), "commits/commit");

  // Object row.
  EXPECT_EQ(ObjectRow::GetKeyFor(ObjectDigest("object")), "objects/object");

  // Reference row
  fxl::StringView destination = "destination";
  char destination_size = static_cast<char>(destination.size());
  fxl::StringView destination_size_str(&destination_size, 1);
  EXPECT_EQ(
      ReferenceRow::GetKeyForObject(ObjectDigest("source"), ObjectDigest("destination"),
                                    KeyPriority::EAGER),
      fxl::Concatenate({"refcounts/", destination_size_str, "destination/object/eager/source"}));
  EXPECT_EQ(
      ReferenceRow::GetKeyForObject(ObjectDigest("source"), ObjectDigest("destination"),
                                    KeyPriority::LAZY),
      fxl::Concatenate({"refcounts/", destination_size_str, "destination/object/lazy/source"}));
  EXPECT_EQ(ReferenceRow::GetKeyForCommit("source", ObjectDigest("destination")),
            fxl::Concatenate({"refcounts/", destination_size_str, "destination/commit/source"}));

  // Unsynced Commit row.
  EXPECT_EQ(UnsyncedCommitRow::GetKeyFor("commit"), "unsynced/commits/commit");

  // Object Status row.
  ObjectIdentifier identifier(1u, 2u, ObjectDigest("object"), nullptr);
  // |ObjectIdentifier|s are serialized using FlatBuffers.
  std::string identifier_serialization =
      "\x10\0\0\0\0\0\n\0\x10\0\x4\0\b\0\f\0\n\0\0\0\x1\0\0\0\x2\0\0\0\x4\0\0\0\x6\0\0\0object\0\0"_s;

  // Object Status: Transient row.
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::TRANSIENT, identifier),
            fxl::Concatenate({"transient/object_digests/", identifier_serialization}));

  // Object Status: Local row.
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::LOCAL, identifier),
            fxl::Concatenate({"local/object_digests/", identifier_serialization}));

  // Object Status: Synced row.
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::SYNCED, identifier),
            fxl::Concatenate({"synced/object_digests/", identifier_serialization}));

  // Sync Metadata row.
  EXPECT_EQ(SyncMetadataRow::GetKeyFor("metadata"), "sync_metadata/metadata");

  // Sync Metadata row.
  EXPECT_EQ(PageIsOnlineRow::kKey, "page_is_online");
}

TEST_F(DbSerialization, MergeRow) {
  const CommitId commit1 = RandomCommitId(environment_.random());
  const CommitId commit2 = RandomCommitId(environment_.random());
  const CommitId commit3 = RandomCommitId(environment_.random());
  EXPECT_THAT(MergeRow::GetKeyFor(commit1, commit2, commit3),
              StartsWith(MergeRow::GetEntriesPrefixFor(commit1, commit2)));
}

TEST_F(DbSerialization, ReferenceRow) {
  const ObjectDigest source = RandomObjectDigest(environment_.random());
  const ObjectDigest destination = RandomObjectDigest(environment_.random());
  const CommitId commit = RandomCommitId(environment_.random());
  // Eager object
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
              StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
              StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
              StartsWith(ReferenceRow::GetEagerKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::EAGER),
              Not(StartsWith(ReferenceRow::GetCommitKeyPrefixFor(destination))));
  // Lazy object
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
              StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
              StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForObject(source, destination, KeyPriority::LAZY),
              StartsWith(ReferenceRow::GetLazyKeyPrefixFor(destination)));
  // Commit
  EXPECT_THAT(ReferenceRow::GetKeyForCommit(commit, destination),
              StartsWith(ReferenceRow::GetKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForCommit(commit, destination),
              StartsWith(ReferenceRow::GetCommitKeyPrefixFor(destination)));
  EXPECT_THAT(ReferenceRow::GetKeyForCommit(commit, destination),
              Not(StartsWith(ReferenceRow::GetObjectKeyPrefixFor(destination))));
}

TEST_F(DbSerialization, ReferenceRowNoCollision) {
  // This test needs to rely on the implementation details of ReferencRow. Its
  // purpose is to fails if we remove the length byte before destination in the
  // encoding, and there is unfortunately no generic way to construct such a
  // collision.
  const ObjectDigest object1 = MakeObjectDigest("");
  const ObjectDigest object2 = MakeObjectDigest(fxl::Concatenate(
      {ReferenceRow::kObjectPrefix, ReferenceRow::kEagerPrefix, object1.Serialize()}));
  EXPECT_EQ(fxl::Concatenate({object2.Serialize(), ReferenceRow::kObjectPrefix,
                              ReferenceRow::kEagerPrefix, object1.Serialize()}),
            fxl::Concatenate({object1.Serialize(), ReferenceRow::kObjectPrefix,
                              ReferenceRow::kEagerPrefix, object2.Serialize()}));
  EXPECT_NE(ReferenceRow::GetKeyForObject(object1, object2, KeyPriority::EAGER),
            ReferenceRow::GetKeyForObject(object2, object1, KeyPriority::EAGER));
}

}  // namespace
}  // namespace storage
