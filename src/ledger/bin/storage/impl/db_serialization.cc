// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/db_serialization.h"

#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_view.h>
#include <zircon/syscalls.h>

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"

namespace storage {

// HeadRow.

constexpr fxl::StringView HeadRow::kPrefix;

std::string HeadRow::GetKeyFor(CommitIdView head) {
  return fxl::Concatenate({kPrefix, head});
}

// MergeRow.

constexpr fxl::StringView MergeRow::kPrefix;

std::string MergeRow::GetEntriesPrefixFor(CommitIdView parent1_id,
                                          CommitIdView parent2_id) {
  auto [parent_min_id, parent_max_id] = std::minmax(parent1_id, parent2_id);
  return fxl::Concatenate({kPrefix, parent_min_id, "/", parent_max_id, "/"});
}

std::string MergeRow::GetKeyFor(CommitIdView merge_commit_id,
                                CommitIdView parent1_id,
                                CommitIdView parent2_id) {
  return fxl::Concatenate(
      {GetEntriesPrefixFor(parent1_id, parent2_id), merge_commit_id});
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

// UnsyncedCommitRow.

constexpr fxl::StringView UnsyncedCommitRow::kPrefix;

std::string UnsyncedCommitRow::GetKeyFor(const CommitId& commit_id) {
  return fxl::Concatenate({kPrefix, commit_id});
}

// ObjectStatusRow.

constexpr fxl::StringView ObjectStatusRow::kTransientPrefix;
constexpr fxl::StringView ObjectStatusRow::kLocalPrefix;
constexpr fxl::StringView ObjectStatusRow::kSyncedPrefix;

std::string ObjectStatusRow::GetKeyFor(
    PageDbObjectStatus object_status,
    const ObjectIdentifier& object_identifier) {
  return fxl::Concatenate(
      {GetPrefixFor(object_status), EncodeObjectIdentifier(object_identifier)});
}

fxl::StringView ObjectStatusRow::GetPrefixFor(
    PageDbObjectStatus object_status) {
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

}  // namespace storage
