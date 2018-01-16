// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/db_serialization.h"

#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/encryption/primitives/rand.h"

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

// TransientObjectRow.

constexpr fxl::StringView TransientObjectRow::kPrefix;

std::string TransientObjectRow::GetKeyFor(ObjectDigestView object_digest) {
  return fxl::Concatenate({kPrefix, object_digest});
}

// LocalObjectRow.

constexpr fxl::StringView LocalObjectRow::kPrefix;

std::string LocalObjectRow::GetKeyFor(ObjectDigestView object_digest) {
  return fxl::Concatenate({kPrefix, object_digest});
}

// SyncMetadataRow.

constexpr fxl::StringView SyncMetadataRow::kPrefix;

std::string SyncMetadataRow::GetKeyFor(fxl::StringView key) {
  return fxl::Concatenate({kPrefix, key});
}

// JournalEntryRow.

constexpr fxl::StringView JournalEntryRow::kExplicitJournalPrefix;
constexpr fxl::StringView JournalEntryRow::kImplicitJournalPrefix;
constexpr fxl::StringView JournalEntryRow::kJournalEntry;
constexpr fxl::StringView JournalEntryRow::kDeletePrefix;
constexpr const char JournalEntryRow::kImplicitIdPrefix;
constexpr const char JournalEntryRow::kExplicitIdPrefix;
constexpr const char JournalEntryRow::kAddPrefix;

std::string JournalEntryRow::NewJournalId(JournalType journal_type) {
  std::string id;
  id.resize(kJournalIdSize);
  id[0] = (journal_type == JournalType::IMPLICIT ? kImplicitIdPrefix
                                                 : kExplicitIdPrefix);
  encryption::RandBytes(&id[1], kJournalIdSize - 1);
  return id;
}

std::string JournalEntryRow::GetKeyFor(const JournalId& journal_id) {
  FXL_DCHECK(journal_id[0] == JournalEntryRow::kImplicitIdPrefix ||
             journal_id[0] == JournalEntryRow::kExplicitIdPrefix);
  if (journal_id[0] == JournalEntryRow::kImplicitIdPrefix) {
    return fxl::Concatenate({kImplicitJournalPrefix, journal_id});
  } else {
    return fxl::Concatenate({kExplicitJournalPrefix, journal_id});
  }
}

std::string JournalEntryRow::GetPrefixFor(const JournalId& journal_id) {
  // TODO(jif): Implement |GetPrefixFor| using |GetKeyFor|.
  // This involves fixing other things, see LE-390.
  return fxl::Concatenate(
      {kExplicitJournalPrefix, journal_id, "/", kJournalEntry});
}

std::string JournalEntryRow::GetKeyFor(const JournalId& id,
                                       fxl::StringView key) {
  return fxl::Concatenate({JournalEntryRow::GetPrefixFor(id), key});
}

std::string JournalEntryRow::GetValueFor(fxl::StringView value,
                                         KeyPriority priority) {
  char priority_byte =
      (priority == KeyPriority::EAGER) ? kEagerPrefix : kLazyPrefix;
  return fxl::Concatenate({{&kAddPrefix, 1}, {&priority_byte, 1}, value});
}

Status JournalEntryRow::ExtractObjectDigest(fxl::StringView db_value,
                                            ObjectDigest* digest) {
  if (db_value[0] == kDeletePrefix[0]) {
    return Status::NOT_FOUND;
  }
  *digest = db_value.substr(kAddPrefixSize).ToString();
  return Status::OK;
}

}  // namespace storage
