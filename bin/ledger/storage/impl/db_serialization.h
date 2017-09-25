// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_

#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

class HeadRow {
 public:
  static constexpr fxl::StringView kPrefix = "heads/";

  static std::string GetKeyFor(CommitIdView head);
};

class CommitRow {
 public:
  static constexpr fxl::StringView kPrefix = "commits/";

  static std::string GetKeyFor(CommitIdView commit_id);
};

class ObjectRow {
 public:
  static constexpr fxl::StringView kPrefix = "objects/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class UnsyncedCommitRow {
 public:
  static constexpr fxl::StringView kPrefix = "unsynced/commits/";

  static std::string GetKeyFor(const CommitId& commit_id);
};

class TransientObjectRow {
 public:
  static constexpr fxl::StringView kPrefix = "transient/object_ids/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class LocalObjectRow {
 public:
  static constexpr fxl::StringView kPrefix = "local/object_ids/";

  static std::string GetKeyFor(ObjectIdView object_id);
};

class ImplicitJournalMetaRow {
 public:
  static constexpr fxl::StringView kPrefix = "journals/implicit/";

  static std::string GetKeyFor(const JournalId& journal_id);
};

class SyncMetadataRow {
 public:
  static constexpr fxl::StringView kPrefix = "sync-metadata/";

  static std::string GetKeyFor(fxl::StringView key);
};

class JournalEntryRow {
 public:
  // Journal keys
  static const size_t kJournalIdSize = 16;
  static constexpr fxl::StringView kPrefix = "journals/";

  static constexpr fxl::StringView kJournalEntry = "entry/";
  static const char kImplicitPrefix = 'I';
  static const char kExplicitPrefix = 'E';
  static const size_t kPrefixSize =
      kPrefix.size() + kJournalIdSize + 1 + kJournalEntry.size();

  // Journal values
  static const char kAddPrefix = 'A';
  static constexpr fxl::StringView kDeletePrefix = "D";
  static const char kLazyPrefix = 'L';
  static const char kEagerPrefix = 'E';
  static const size_t kAddPrefixSize = 2;

  static std::string NewJournalId(JournalType journal_type);

  static std::string GetPrefixFor(const JournalId& journal_id);

  static std::string GetKeyFor(const JournalId& id, fxl::StringView key);

  static std::string GetValueFor(fxl::StringView value, KeyPriority priority);

  static Status ExtractObjectId(fxl::StringView db_value, ObjectId* id);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_
