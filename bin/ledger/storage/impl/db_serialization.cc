// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/db_serialization.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

// HeadRow.

constexpr ftl::StringView HeadRow::kPrefix;

std::string HeadRow::GetKeyFor(CommitIdView head) {
  return ftl::Concatenate({kPrefix, head});
}

// CommitRow.

constexpr ftl::StringView CommitRow::kPrefix;

std::string CommitRow::GetKeyFor(CommitIdView commit_id) {
  return ftl::Concatenate({kPrefix, commit_id});
}

// ObjectRow.

constexpr ftl::StringView ObjectRow::kPrefix;

std::string ObjectRow::GetKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kPrefix, object_id});
}

// UnsyncedCommitRow.

constexpr ftl::StringView UnsyncedCommitRow::kPrefix;

std::string UnsyncedCommitRow::GetKeyFor(const CommitId& commit_id) {
  return ftl::Concatenate({kPrefix, commit_id});
}

// TransientObjectRow.

constexpr ftl::StringView TransientObjectRow::kPrefix;

std::string TransientObjectRow::GetKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kPrefix, object_id});
}

// LocalObjectRow.

constexpr ftl::StringView LocalObjectRow::kPrefix;

std::string LocalObjectRow::GetKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kPrefix, object_id});
}

// ImplicitJournalMetaRow.

constexpr ftl::StringView ImplicitJournalMetaRow::kPrefix;

std::string ImplicitJournalMetaRow::GetKeyFor(const JournalId& journal_id) {
  return ftl::Concatenate({kPrefix, journal_id});
}

// SyncMetadataRow.

constexpr ftl::StringView SyncMetadataRow::kPrefix;

std::string SyncMetadataRow::GetKeyFor(ftl::StringView key) {
  return ftl::Concatenate({kPrefix, key});
}

// JournalEntryRow.

constexpr ftl::StringView JournalEntryRow::kPrefix;
constexpr ftl::StringView JournalEntryRow::kJournalEntry;
constexpr ftl::StringView JournalEntryRow::kDeletePrefix;
constexpr const char JournalEntryRow::kImplicitPrefix;
constexpr const char JournalEntryRow::kAddPrefix;

std::string JournalEntryRow::NewJournalId(JournalType journal_type) {
  std::string id;
  id.resize(kJournalIdSize);
  id[0] = (journal_type == JournalType::IMPLICIT ? kImplicitPrefix
                                                 : kExplicitPrefix);
  glue::RandBytes(&id[1], kJournalIdSize - 1);
  return id;
}

std::string JournalEntryRow::GetPrefixFor(const JournalId& journal_id) {
  return ftl::Concatenate({kPrefix, journal_id, "/", kJournalEntry});
}

std::string JournalEntryRow::GetKeyFor(const JournalId& id,
                                       ftl::StringView key) {
  return ftl::Concatenate({JournalEntryRow::GetPrefixFor(id), key});
}

std::string JournalEntryRow::GetValueFor(ftl::StringView value,
                                         KeyPriority priority) {
  char priority_byte =
      (priority == KeyPriority::EAGER) ? kEagerPrefix : kLazyPrefix;
  return ftl::Concatenate({{&kAddPrefix, 1}, {&priority_byte, 1}, value});
}

Status JournalEntryRow::ExtractObjectId(ftl::StringView db_value,
                                        ObjectId* id) {
  if (db_value[0] == kDeletePrefix[0]) {
    return Status::NOT_FOUND;
  }
  *id = db_value.substr(kAddPrefixSize).ToString();
  return Status::OK;
}

}  // namespace storage
