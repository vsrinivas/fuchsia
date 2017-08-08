// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _APPS_LEDGER_SRC_STORAGE_IMPL_DB_SERIALIZATION_H_
#define _APPS_LEDGER_SRC_STORAGE_IMPL_DB_SERIALIZATION_H_

#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

class HeadRow {
 public:
  static constexpr ftl::StringView kPrefix = "heads/";

  static std::string GetKeyFor(CommitIdView head);
};

class CommitRow {
 public:
  static constexpr ftl::StringView kPrefix = "commits/";

  static std::string GetKeyFor(CommitIdView commit_id);
};

class ObjectRow {
 public:
  static constexpr ftl::StringView kPrefix = "objects/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class UnsyncedCommitRow {
 public:
  static constexpr ftl::StringView kPrefix = "unsynced/commits/";

  static std::string GetKeyFor(const CommitId& commit_id);
};

class TransientObjectRow {
 public:
  static constexpr ftl::StringView kPrefix = "transient/object_ids/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class LocalObjectRow {
 public:
  static constexpr ftl::StringView kPrefix = "local/object_ids/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class ImplicitJournalMetaRow {
 public:
  static constexpr ftl::StringView kPrefix = "journals/implicit/";

  static std::string GetKeyFor(const JournalId& journal_id);
};

class SyncMetadataRow {
 public:
  static constexpr ftl::StringView kPrefix = "sync-metadata/";

  static std::string GetKeyFor(ftl::StringView key);
};

class JournalEntryRow {
 public:
  // Journal keys
  static const size_t kJournalIdSize = 16;
  static constexpr ftl::StringView kPrefix = "journals/";

  static constexpr ftl::StringView kJournalEntry = "entry/";
  static const char kImplicitPrefix = 'I';
  static const char kExplicitPrefix = 'E';
  static const size_t kPrefixSize =
      kPrefix.size() + kJournalIdSize + 1 + kJournalEntry.size();

  // Journal values
  static const char kAddPrefix = 'A';
  static constexpr ftl::StringView kDeletePrefix = "D";
  static const char kLazyPrefix = 'L';
  static const char kEagerPrefix = 'E';
  static const size_t kAddPrefixSize = 2;

  static std::string NewJournalId(JournalType journal_type);

  static std::string GetPrefixFor(const JournalId& journal_id);

  static std::string GetKeyFor(const JournalId& id, ftl::StringView key);

  static std::string GetValueFor(ftl::StringView value, KeyPriority priority);

  static Status ExtractObjectId(ftl::StringView db_value, ObjectId* id);
};

}  // namespace storage

#endif  // _APPS_LEDGER_SRC_STORAGE_IMPL_DB_SERIALIZATION_H_
