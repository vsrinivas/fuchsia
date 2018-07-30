// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/db_serialization.h"

#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_view.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"

namespace storage {

// HeadRow.

constexpr fxl::StringView HeadRow::kPrefix;

std::string HeadRow::GetKeyFor(CommitIdView head) {
  return fxl::Concatenate({kPrefix, head});
}

// CommitRow.

constexpr fxl::StringView CommitRow::kPrefix;

std::string CommitRow::GetKeyFor(CommitIdView commit_id) {
  return fxl::Concatenate({kPrefix, commit_id});
}

// ObjectRow.

constexpr fxl::StringView ObjectRow::kPrefix;

std::string ObjectRow::GetKeyFor(ObjectDigestView object_digest) {
  return fxl::Concatenate({kPrefix, object_digest});
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

// ImplicitJournalMetadataRow.

constexpr fxl::StringView ImplicitJournalMetadataRow::kPrefix;

std::string ImplicitJournalMetadataRow::GetKeyFor(const JournalId& journal_id) {
  return fxl::Concatenate({kPrefix, journal_id});
}

// SyncMetadataRow.

constexpr fxl::StringView SyncMetadataRow::kPrefix;

std::string SyncMetadataRow::GetKeyFor(fxl::StringView key) {
  return fxl::Concatenate({kPrefix, key});
}

// JournalEntryRow.

constexpr fxl::StringView JournalEntryRow::kPrefix;
constexpr fxl::StringView JournalEntryRow::kJournalEntry;
constexpr fxl::StringView JournalEntryRow::kDeletePrefix;
constexpr char JournalEntryRow::kImplicitPrefix;
constexpr char JournalEntryRow::kExplicitPrefix;
constexpr char JournalEntryRow::kAddPrefix;
constexpr char JournalEntryRow::kClear;

std::string JournalEntryRow::NewJournalId(JournalType journal_type) {
  std::string id;
  id.resize(kJournalIdSize);
  id[0] = (journal_type == JournalType::IMPLICIT ? kImplicitPrefix
                                                 : kExplicitPrefix);
  zx_cprng_draw(&id[1], kJournalIdSize - 1);
  return id;
}

std::string JournalEntryRow::GetPrefixFor(const JournalId& journal_id) {
  return fxl::Concatenate({kPrefix, journal_id, "/"});
}

std::string JournalEntryRow::GetEntriesPrefixFor(const JournalId& journal_id) {
  return fxl::Concatenate(
      {JournalEntryRow::GetPrefixFor(journal_id), kJournalEntry});
}

std::string JournalEntryRow::GetKeyFor(const JournalId& id,
                                       fxl::StringView key) {
  return fxl::Concatenate({JournalEntryRow::GetEntriesPrefixFor(id), key});
}

std::string JournalEntryRow::GetClearMarkerKey(const JournalId& id) {
  return fxl::Concatenate({JournalEntryRow::GetPrefixFor(id), {&kClear, 1}});
}

std::string JournalEntryRow::GetValueFor(
    const ObjectIdentifier& object_identifier, KeyPriority priority) {
  char priority_byte =
      (priority == KeyPriority::EAGER) ? kEagerPrefix : kLazyPrefix;
  return fxl::Concatenate({{&kAddPrefix, 1},
                           {&priority_byte, 1},
                           EncodeObjectIdentifier(object_identifier)});
}

Status JournalEntryRow::ExtractObjectIdentifier(
    fxl::StringView db_value, ObjectIdentifier* object_identifier) {
  if (db_value[0] == kDeletePrefix[0]) {
    return Status::NOT_FOUND;
  }
  if (!DecodeObjectIdentifier(db_value.substr(kAddPrefixSize),
                              object_identifier)) {
    return Status::FORMAT_ERROR;
  }
  return Status::OK;
}

// PageIsOnlineRow.

constexpr fxl::StringView PageIsOnlineRow::kKey;

}  // namespace storage
