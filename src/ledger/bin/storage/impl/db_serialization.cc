// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include <zircon/syscalls.h>

#include <limits>

#include "src/ledger/bin/storage/impl/constants.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// HeadRow.

constexpr absl::string_view HeadRow::kPrefix;

std::string HeadRow::GetKeyFor(CommitIdView head) { return absl::StrCat(kPrefix, head); }

// MergeRow.

constexpr absl::string_view MergeRow::kPrefix;

std::string MergeRow::GetEntriesPrefixFor(CommitIdView parent1_id, CommitIdView parent2_id) {
  auto [parent_min_id, parent_max_id] = std::minmax(parent1_id, parent2_id);
  return absl::StrCat(kPrefix, parent_min_id, "/", parent_max_id, "/");
}

std::string MergeRow::GetKeyFor(CommitIdView parent1_id, CommitIdView parent2_id,
                                CommitIdView merge_commit_id) {
  return absl::StrCat(GetEntriesPrefixFor(parent1_id, parent2_id), merge_commit_id);
}

// CommitRow.

constexpr absl::string_view CommitRow::kPrefix;

std::string CommitRow::GetKeyFor(CommitIdView commit_id) {
  return absl::StrCat(kPrefix, commit_id);
}

// ObjectRow.

constexpr absl::string_view ObjectRow::kPrefix;

std::string ObjectRow::GetKeyFor(const ObjectDigest& object_digest) {
  return absl::StrCat(kPrefix, object_digest.Serialize());
}

// ReferenceRow.

constexpr absl::string_view ReferenceRow::kPrefix;
constexpr absl::string_view ReferenceRow::kObjectPrefix;
constexpr absl::string_view ReferenceRow::kEagerPrefix;
constexpr absl::string_view ReferenceRow::kLazyPrefix;
constexpr absl::string_view ReferenceRow::kCommitPrefix;

std::string ReferenceRow::GetKeyForObject(const ObjectDigest& source,
                                          const ObjectDigest& destination, KeyPriority priority) {
  return absl::StrCat(priority == KeyPriority::EAGER ? GetEagerKeyPrefixFor(destination)
                                                     : GetLazyKeyPrefixFor(destination),
                      source.Serialize());
}

std::string ReferenceRow::GetKeyForCommit(CommitIdView source, const ObjectDigest& destination) {
  return absl::StrCat(GetCommitKeyPrefixFor(destination), source);
}

std::string ReferenceRow::GetKeyPrefixFor(const ObjectDigest& destination) {
  // Check that |destination| size is fixed, ie. |destination| is not a reference to an inline
  // object, to ensure there is no ambiguity in the encoding.
  LEDGER_DCHECK(destination.Serialize().size() == kStorageHashSize + 1);
  return absl::StrCat(kPrefix, destination.Serialize());
};

std::string ReferenceRow::GetObjectKeyPrefixFor(const ObjectDigest& destination) {
  return absl::StrCat(GetKeyPrefixFor(destination), kObjectPrefix);
};

std::string ReferenceRow::GetEagerKeyPrefixFor(const ObjectDigest& destination) {
  return absl::StrCat(GetObjectKeyPrefixFor(destination), kEagerPrefix);
};

std::string ReferenceRow::GetLazyKeyPrefixFor(const ObjectDigest& destination) {
  return absl::StrCat(GetObjectKeyPrefixFor(destination), kLazyPrefix);
};

std::string ReferenceRow::GetCommitKeyPrefixFor(const ObjectDigest& destination) {
  return absl::StrCat(GetKeyPrefixFor(destination), kCommitPrefix);
};

// UnsyncedCommitRow.

constexpr absl::string_view UnsyncedCommitRow::kPrefix;

std::string UnsyncedCommitRow::GetKeyFor(CommitIdView commit_id) {
  return absl::StrCat(kPrefix, commit_id);
}

// ObjectStatusRow.

constexpr absl::string_view ObjectStatusRow::kTransientPrefix;
constexpr absl::string_view ObjectStatusRow::kLocalPrefix;
constexpr absl::string_view ObjectStatusRow::kSyncedPrefix;

std::string ObjectStatusRow::GetKeyFor(PageDbObjectStatus object_status,
                                       const ObjectIdentifier& object_identifier) {
  return absl::StrCat(GetPrefixFor(object_status),
                      EncodeDigestPrefixedObjectIdentifier(object_identifier));
}

std::string ObjectStatusRow::GetPrefixFor(PageDbObjectStatus object_status,
                                          const ObjectDigest& object_digest) {
  return absl::StrCat(GetPrefixFor(object_status), object_digest.Serialize());
}

absl::string_view ObjectStatusRow::GetPrefixFor(PageDbObjectStatus object_status) {
  switch (object_status) {
    case PageDbObjectStatus::UNKNOWN:
      LEDGER_NOTREACHED();
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

constexpr absl::string_view SyncMetadataRow::kPrefix;

std::string SyncMetadataRow::GetKeyFor(absl::string_view key) { return absl::StrCat(kPrefix, key); }

// PageIsOnlineRow.

constexpr absl::string_view PageIsOnlineRow::kKey;

// ClockRow.

constexpr absl::string_view ClockRow::kDeviceIdKey;
constexpr absl::string_view ClockRow::kEntriesKey;

// RemoteCommitIdToLocalRow.

constexpr absl::string_view RemoteCommitIdToLocalRow::kPrefix;

std::string RemoteCommitIdToLocalRow::GetKeyFor(absl::string_view remote_commit_id) {
  return absl::StrCat(kPrefix, remote_commit_id);
}

}  // namespace storage
