// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace storage {
namespace {

using ::testing::Not;
using ::testing::StartsWith;

using DbSerialization = ::ledger::TestWithEnvironment;

// Allows to create correct std::strings with \0 bytes inside from C-style string constants.
std::string operator"" _s(const char* str, size_t size) { return std::string(str, size); }

// This test makes sure nothing has changed in the rows serialization. If this breaks, it means
// action needs to be taken to avoid breaking backward compatibility.
TEST_F(DbSerialization, SerializationVersionControl) {
  // Head row (prefix ' ').
  EXPECT_EQ(HeadRow::GetKeyFor("head"), " head");

  // Merge row (prefix '!').
  EXPECT_EQ(MergeRow::GetKeyFor("parent1", "parent2", "merge"), "!parent1/parent2/merge");

  // Commit row (prefix '"').
  EXPECT_EQ(CommitRow::GetKeyFor("commit"), "\"commit");

  // Object row (prefix '#').
  EXPECT_EQ(ObjectRow::GetKeyFor(ObjectDigest("object")), "#object");

  // Reference row (prefix '$').
  // Destination object digest must be exactly 32+1 bytes long (ie. non-inline).
  EXPECT_EQ(ReferenceRow::GetKeyForObject(ObjectDigest("source"),
                                          ObjectDigest("0123456789ABCDEF0123456789ABCDEF0"),
                                          KeyPriority::EAGER),
            "$0123456789ABCDEF0123456789ABCDEF0  source");
  EXPECT_EQ(ReferenceRow::GetKeyForObject(ObjectDigest("source"),
                                          ObjectDigest("0123456789ABCDEF0123456789ABCDEF0"),
                                          KeyPriority::LAZY),
            "$0123456789ABCDEF0123456789ABCDEF0 !source");
  EXPECT_EQ(
      ReferenceRow::GetKeyForCommit("source", ObjectDigest("0123456789ABCDEF0123456789ABCDEF0")),
      "$0123456789ABCDEF0123456789ABCDEF0!source");

  // Unsynced Commit row (prefix '%').
  EXPECT_EQ(UnsyncedCommitRow::GetKeyFor("commit"), "\%commit");

  // Object Status rows.
  ObjectIdentifier identifier(1u, ObjectDigest("0123456789ABCDEF0123456789ABCDEF0"), nullptr);
  // Identifier is serialized as object-digest concatenated with the serialization of key-index.
  std::string identifier_serialization = "0123456789ABCDEF0123456789ABCDEF0\x1\0\0\0"_s;

  // Object Status: Transient row (prefix '&').
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::TRANSIENT, identifier),
            fxl::Concatenate({"&", identifier_serialization}));

  // Object Status: Local row (prefix '\'').
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::LOCAL, identifier),
            fxl::Concatenate({"'", identifier_serialization}));

  // Object Status: Synced row (prefix '(').
  EXPECT_EQ(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::SYNCED, identifier),
            fxl::Concatenate({"(", identifier_serialization}));

  // Sync Metadata row (prefix ')').
  EXPECT_EQ(SyncMetadataRow::GetKeyFor("metadata"), ")metadata");

  // Page is online row (prefix '*').
  EXPECT_EQ(PageIsOnlineRow::kKey, "*");

  // Clock row: device id (prefix '+').
  EXPECT_EQ(ClockRow::kDeviceIdKey, "+");

  // Clock row: entries (prefix ',').
  EXPECT_EQ(ClockRow::kEntriesKey, ",");

  // Remote commit id to local row (prefix '-').
  EXPECT_EQ(RemoteCommitIdToLocalRow::GetKeyFor("remote_commit_id"), "-remote_commit_id");
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

TEST_F(DbSerialization, ObjectStatusRow) {
  storage::fake::FakeObjectIdentifierFactory factory;
  const ObjectIdentifier identifier = RandomObjectIdentifier(environment_.random(), &factory);

  for (PageDbObjectStatus status :
       {PageDbObjectStatus::TRANSIENT, PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED}) {
    EXPECT_THAT(ObjectStatusRow::GetKeyFor(status, identifier),
                StartsWith(ObjectStatusRow::GetPrefixFor(status, identifier.object_digest())));
  }
}

}  // namespace
}  // namespace storage
