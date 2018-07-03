// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_TYPES_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_TYPES_H_

#include <ostream>
#include <string>

#include <lib/fxl/strings/string_view.h>

#include "peridot/lib/convert/convert.h"

namespace storage {

using PageId = std::string;
using PageIdView = convert::ExtendedStringView;
using CommitId = std::string;
using CommitIdView = convert::ExtendedStringView;
using ObjectDigest = std::string;
using ObjectDigestView = convert::ExtendedStringView;
using JournalId = std::string;
using JournalIdView = convert::ExtendedStringView;

// The priority at which the key value is downloaded, and the cache policy.
enum class KeyPriority {
  EAGER,
  LAZY,
};

// The identifier of an object. This contains the digest of the object, as well
// as the information needed to hide its name and encrypt its content.
struct ObjectIdentifier {
  uint32_t key_index;
  uint32_t deletion_scope_id;
  ObjectDigest object_digest;
};

bool operator==(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
bool operator!=(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
bool operator<(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs);
std::ostream& operator<<(std::ostream& os, const ObjectIdentifier& e);

// An entry in a commit.
struct Entry {
  std::string key;
  ObjectIdentifier object_identifier;
  KeyPriority priority;
};

bool operator==(const Entry& lhs, const Entry& rhs);
bool operator!=(const Entry& lhs, const Entry& rhs);
std::ostream& operator<<(std::ostream& os, const Entry& e);

// A change between two commit contents.
struct EntryChange {
  Entry entry;
  bool deleted;
};

bool operator==(const EntryChange& lhs, const EntryChange& rhs);
bool operator!=(const EntryChange& lhs, const EntryChange& rhs);
std::ostream& operator<<(std::ostream& os, const EntryChange& e);

// A change between 3 commit contents.
struct ThreeWayChange {
  std::unique_ptr<Entry> base;
  std::unique_ptr<Entry> left;
  std::unique_ptr<Entry> right;
};

bool operator==(const ThreeWayChange& lhs, const ThreeWayChange& rhs);
bool operator!=(const ThreeWayChange& lhs, const ThreeWayChange& rhs);
std::ostream& operator<<(std::ostream& os, const ThreeWayChange& e);

enum class ChangeSource { LOCAL, P2P, CLOUD };

enum class JournalType { IMPLICIT, EXPLICIT };

enum class Status {
  // User visible status.
  OK,
  IO_ERROR,
  NOT_FOUND,

  // Internal status.
  FORMAT_ERROR,
  ILLEGAL_STATE,
  INTERNAL_IO_ERROR,
  INTERRUPTED,
  NOT_CONNECTED_ERROR,
  NO_SUCH_CHILD,
  OBJECT_DIGEST_MISMATCH,

  // Temporary status or status for tests.
  NOT_IMPLEMENTED,
};

fxl::StringView StatusToString(Status status);
std::ostream& operator<<(std::ostream& os, Status status);

}  // namespace storage
#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_TYPES_H_
