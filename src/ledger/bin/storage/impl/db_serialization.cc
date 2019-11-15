// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include <zircon/syscalls.h>

#include <limits>

#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {

// HeadRow.

constexpr fxl::StringView HeadRow::kPrefix;

std::string HeadRow::GetKeyFor(CommitIdView head) { return fxl::Concatenate({kPrefix, head}); }

// MergeRow.

constexpr fxl::StringView MergeRow::kPrefix;

std::string MergeRow::GetEntriesPrefixFor(CommitIdView parent1_id, CommitIdView parent2_id) {
  auto [parent_min_id, parent_max_id] = std::minmax(parent1_id, parent2_id);
  return fxl::Concatenate({kPrefix, parent_min_id, "/", parent_max_id, "/"});
}

std::string MergeRow::GetKeyFor(CommitIdView parent1_id, CommitIdView parent2_id,
                                CommitIdView merge_commit_id) {
  return fxl::Concatenate({GetEntriesPrefixFor(parent1_id, parent2_id), merge_commit_id});
}

// CommitRow.

constexpr fxl::StringView CommitRow::kPrefix;

std::string CommitRow::GetKeyFor(CommitIdView commit_id) {
  return fxl::Concatenate({kPrefix, commit_id});
}

// ObjectRow.

constexpr fxl::StringView ObjectRow::kPrefix;

std::string ObjectRow::GetKeyFor(const ObjectDigest& object_digest) {
  return fxl::Concatenate({kPrefix, object_digest.Serialize()});
}

// ReferenceRow.

constexpr fxl::StringView ReferenceRow::kPrefix;
constexpr fxl::StringView ReferenceRow::kObjectPrefix;
constexpr fxl::StringView ReferenceRow::kEagerPrefix;
constexpr fxl::StringView ReferenceRow::kLazyPrefix;
constexpr fxl::StringView ReferenceRow::kCommitPrefix;

std::string ReferenceRow::GetKeyForObject(const ObjectDigest& source,
                                          const ObjectDigest& destination, KeyPriority priority) {
  return fxl::Concatenate({priority == KeyPriority::EAGER ? GetEagerKeyPrefixFor(destination)
                                                          : GetLazyKeyPrefixFor(destination),
                           source.Serialize()});
}

std::string ReferenceRow::GetKeyForCommit(CommitIdView source, const ObjectDigest& destination) {
  return fxl::Concatenate({GetCommitKeyPrefixFor(destination), source});
}

std::string ReferenceRow::GetKeyPrefixFor(const ObjectDigest& destination) {
  // Check that |destination| size is fixed, ie. |destination| is not a reference to an inline
  // object, to ensure there is no ambiguity in the encoding.
  FXL_DCHECK(destination.Serialize().size() == kStorageHashSize + 1);
  return fxl::Concatenate({kPrefix, destination.Serialize()});
};

std::string ReferenceRow::GetObjectKeyPrefixFor(const ObjectDigest& destination) {
  return fxl::Concatenate({GetKeyPrefixFor(destination), kObjectPrefix});
};

std::string ReferenceRow::GetEagerKeyPrefixFor(const ObjectDigest& destination) {
  return fxl::Concatenate({GetObjectKeyPrefixFor(destination), kEagerPrefix});
};

std::string ReferenceRow::GetLazyKeyPrefixFor(const ObjectDigest& destination) {
  return fxl::Concatenate({GetObjectKeyPrefixFor(destination), kLazyPrefix});
};

std::string ReferenceRow::GetCommitKeyPrefixFor(const ObjectDigest& destination) {
  return fxl::Concatenate({GetKeyPrefixFor(destination), kCommitPrefix});
};

// UnsyncedCommitRow.

constexpr fxl::StringView UnsyncedCommitRow::kPrefix;

std::string UnsyncedCommitRow::GetKeyFor(CommitIdView commit_id) {
  return fxl::Concatenate({kPrefix, commit_id});
}

// ObjectStatusRow.

constexpr fxl::StringView ObjectStatusRow::kTransientPrefix;
constexpr fxl::StringView ObjectStatusRow::kLocalPrefix;
constexpr fxl::StringView ObjectStatusRow::kSyncedPrefix;

std::string ObjectStatusRow::GetKeyFor(PageDbObjectStatus object_status,
                                       const ObjectIdentifier& object_identifier) {
  return fxl::Concatenate(
      {GetPrefixFor(object_status), EncodeDigestPrefixedObjectIdentifier(object_identifier)});
}

std::string ObjectStatusRow::GetPrefixFor(PageDbObjectStatus object_status,
                                          const ObjectDigest& object_digest) {
  return fxl::Concatenate({GetPrefixFor(object_status), object_digest.Serialize()});
}

fxl::StringView ObjectStatusRow::GetPrefixFor(PageDbObjectStatus object_status) {
  switch (object_status) {
    case PageDbObjectStatus::UNKNOWN:
      FXL_NOTREACHED();
      return "";
    case PageDbObjectStatus::TRANSIENT:
      return kTransientPrefix;
    case PageDbObjectStatus::LOCAL:
      return kLocalPrefix;
    case PageDbObjectStatus::SYNCED:
      return kSyncedPrefix;
  }
}

// SyncMetadataRow.

constexpr fxl::StringView SyncMetadataRow::kPrefix;

std::string SyncMetadataRow::GetKeyFor(fxl::StringView key) {
  return fxl::Concatenate({kPrefix, key});
}

// PageIsOnlineRow.

constexpr fxl::StringView PageIsOnlineRow::kKey;

// ClockRow.

constexpr fxl::StringView ClockRow::kDeviceIdKey;
constexpr fxl::StringView ClockRow::kEntriesKey;

// RemoteCommitIdToLocalRow.

constexpr fxl::StringView RemoteCommitIdToLocalRow::kPrefix;

std::string RemoteCommitIdToLocalRow::GetKeyFor(fxl::StringView remote_commit_id) {
  return fxl::Concatenate({kPrefix, remote_commit_id});
}

}  // namespace storage
