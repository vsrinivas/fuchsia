// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/impl/page_db.h"
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

  static std::string GetKeyFor(ObjectDigestView object_digest);
};

class UnsyncedCommitRow {
 public:
  static constexpr fxl::StringView kPrefix = "unsynced/commits/";

  static std::string GetKeyFor(const CommitId& commit_id);
};

class ObjectStatusRow {
 public:
  static constexpr fxl::StringView kTransientPrefix =
      "transient/object_digests/";
  static constexpr fxl::StringView kLocalPrefix = "local/object_digests/";
  static constexpr fxl::StringView kSyncedPrefix = "synced/object_digests/";

  static std::string GetKeyFor(PageDbObjectStatus object_status,
                               const ObjectIdentifier& object_identifier);

 private:
  static fxl::StringView GetPrefixFor(PageDbObjectStatus object_status);
};

class ImplicitJournalMetadataRow {
 public:
  static constexpr fxl::StringView kPrefix = "journals/implicit_metadata/";

  static std::string GetKeyFor(const JournalId& journal_id);
};

class SyncMetadataRow {
 public:
  static constexpr fxl::StringView kPrefix = "sync_metadata/";

  static std::string GetKeyFor(fxl::StringView key);
};

class JournalEntryRow {
 public:
  // Journal keys
  static const size_t kJournalIdSize = 16;
  static constexpr fxl::StringView kPrefix = "journals/";

  static constexpr fxl::StringView kJournalEntry = "entry/";
  static constexpr char kImplicitPrefix = 'I';
  static constexpr char kExplicitPrefix = 'E';
  static const size_t kPrefixSize =
      kPrefix.size() + kJournalIdSize + 1 + kJournalEntry.size();

  // Journal values
  static constexpr char kAddPrefix = 'A';
  static constexpr fxl::StringView kDeletePrefix = "D";
  static const char kLazyPrefix = 'L';
  static const char kEagerPrefix = 'E';
  static const size_t kAddPrefixSize = 2;

  static std::string NewJournalId(JournalType journal_type);

  static std::string GetPrefixFor(const JournalId& journal_id);

  static std::string GetKeyFor(const JournalId& id, fxl::StringView key);

  static std::string GetValueFor(const ObjectIdentifier& object_identifier,
                                 KeyPriority priority);

  static Status ExtractObjectIdentifier(fxl::StringView db_value,
                                        ObjectIdentifier* object_identifier);
};

class PageIsOnlineRow {
 public:
  static constexpr fxl::StringView kKey = "page_is_online";
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_DB_SERIALIZATION_H_
