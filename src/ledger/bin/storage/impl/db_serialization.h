// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_DB_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_DB_SERIALIZATION_H_

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/storage/impl/page_db.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {

// The prefix to be used in rows depending on their type. ' ' (space) is used as the value of the
// first one as a way to make rows easier to read on debug information.
//
// Important: Always append at the end. Do not reorder, do not delete.
enum class RowType : uint8_t {
  HEADS = ' ',
  MERGES,                     // '!'
  COMMITS,                    // '"'
  OBJECTS,                    // '#'
  REFCOUNTS,                  // '$'
  UNSYNCED_COMMIT,            // '%'
  TRANCIENT_OBJECT_DIGEST,    // '&'
  LOCAL_OBJECT_DIGEST,        // '\''
  SYNCED_OBJECT_DIGEST,       // '('
  SYNC_METADATA,              // ')'
  PAGE_IS_ONLINE,             // '*'
  CLOCK_DEVICE_ID,            // '+'
  CLOCK_ENTRIES,              // ','
  REMOTE_COMMIT_ID_TO_LOCAL,  // '-'
};

class HeadRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::HEADS);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(CommitIdView head);
};

class MergeRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::MERGES);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(CommitIdView parent1_id, CommitIdView parent2_id,
                               CommitIdView merge_commit_id);
  static std::string GetEntriesPrefixFor(CommitIdView parent1_id, CommitIdView parent2_id);
};

class CommitRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::COMMITS);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(CommitIdView commit_id);
};

class ObjectRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::OBJECTS);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(const ObjectDigest& object_digest);
};

// Serialization of rows for reference counting.
// The methods in this class are valid only for non-inline |destination| pieces.
class ReferenceRow {
 private:
  enum class Type : uint8_t {
    OBJECT = ' ',
    COMMIT,  // '!'
  };
  enum class Priority : uint8_t {
    EAGER = ' ',
    LAZY,  // '!'
  };
  static constexpr char kPrefixChar = static_cast<char>(RowType::REFCOUNTS);
  static constexpr char kCommitPrefixChar = static_cast<char>(Type::COMMIT);
  static constexpr char kObjectPrefixChar = static_cast<char>(Type::OBJECT);
  static constexpr char kEagerPrefixChar = static_cast<char>(Priority::EAGER);
  static constexpr char kLazyPrefixChar = static_cast<char>(Priority::LAZY);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);
  static constexpr fxl::StringView kCommitPrefix = fxl::StringView(&kCommitPrefixChar, 1);
  static constexpr fxl::StringView kObjectPrefix = fxl::StringView(&kObjectPrefixChar, 1);
  static constexpr fxl::StringView kEagerPrefix = fxl::StringView(&kEagerPrefixChar, 1);
  static constexpr fxl::StringView kLazyPrefix = fxl::StringView(&kLazyPrefixChar, 1);

  // Returns key for object-object links.
  static std::string GetKeyForObject(const ObjectDigest& source, const ObjectDigest& destination,
                                     KeyPriority priority);

  // Returns key for commit-object links.
  static std::string GetKeyForCommit(CommitIdView source, const ObjectDigest& destination);

  // Returns key prefix for all links to |destination|.
  static std::string GetKeyPrefixFor(const ObjectDigest& destination);
  // Returns key prefix for object links to |destination|.
  static std::string GetObjectKeyPrefixFor(const ObjectDigest& destination);
  // Returns key prefix for eager object links to |destination|.
  static std::string GetEagerKeyPrefixFor(const ObjectDigest& destination);
  // Returns key prefix for lazy object links to |destination|.
  static std::string GetLazyKeyPrefixFor(const ObjectDigest& destination);
  // Returns key prefix for commit links to |destination|.
  static std::string GetCommitKeyPrefixFor(const ObjectDigest& destination);
};

class UnsyncedCommitRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::UNSYNCED_COMMIT);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(CommitIdView commit_id);
};

// Serialization of rows holding object synchronization status.
// The methods in this class are valid only for non-inline objects.
class ObjectStatusRow {
 private:
  static constexpr char kTransientPrefixChar = static_cast<char>(RowType::TRANCIENT_OBJECT_DIGEST);
  static constexpr char kLocalPrefixChar = static_cast<char>(RowType::LOCAL_OBJECT_DIGEST);
  static constexpr char kSyncedPrefixChar = static_cast<char>(RowType::SYNCED_OBJECT_DIGEST);

 public:
  static constexpr fxl::StringView kTransientPrefix = fxl::StringView(&kTransientPrefixChar, 1);
  static constexpr fxl::StringView kLocalPrefix = fxl::StringView(&kLocalPrefixChar, 1);
  static constexpr fxl::StringView kSyncedPrefix = fxl::StringView(&kSyncedPrefixChar, 1);

  static std::string GetKeyFor(PageDbObjectStatus object_status,
                               const ObjectIdentifier& object_identifier);

  static std::string GetPrefixFor(PageDbObjectStatus object_status,
                                  const ObjectDigest& object_digest);

 private:
  static fxl::StringView GetPrefixFor(PageDbObjectStatus object_status);
};

class SyncMetadataRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::SYNC_METADATA);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(fxl::StringView key);
};

class PageIsOnlineRow {
 private:
  static constexpr char kKeyChar = static_cast<char>(RowType::PAGE_IS_ONLINE);

 public:
  static constexpr fxl::StringView kKey = fxl::StringView(&kKeyChar, 1);
};

class ClockRow {
 private:
  static constexpr char kClockDeviceIdChar = static_cast<char>(RowType::CLOCK_DEVICE_ID);
  static constexpr char kClockEntriesPrefixChar = static_cast<char>(RowType::CLOCK_ENTRIES);

 public:
  static constexpr fxl::StringView kDeviceIdKey = fxl::StringView(&kClockDeviceIdChar, 1);
  static constexpr fxl::StringView kEntriesKey = fxl::StringView(&kClockEntriesPrefixChar, 1);
};

class RemoteCommitIdToLocalRow {
 private:
  static constexpr char kPrefixChar = static_cast<char>(RowType::REMOTE_COMMIT_ID_TO_LOCAL);

 public:
  static constexpr fxl::StringView kPrefix = fxl::StringView(&kPrefixChar, 1);

  static std::string GetKeyFor(fxl::StringView remote_commit_id);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_DB_SERIALIZATION_H_
