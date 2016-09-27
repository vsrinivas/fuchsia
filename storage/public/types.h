// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_TYPES_H_
#define APPS_LEDGER_STORAGE_PUBLIC_TYPES_H_

#include <string>

namespace storage {

using PageId = std::string;
using CommitId = std::string;
using ObjectId = std::string;
using JournalId = std::string;

// The priority at which the key value is downloaded, and the cache policy.
enum class KeyPriority {
  EAGER,
  LAZY,
};

// An entry in a commit.
struct Entry {
  std::string key;
  ObjectId blob_id;
  KeyPriority priority;
};

bool operator==(const Entry& lhs, const Entry& rhs);
bool operator!=(const Entry& lhs, const Entry& rhs);

// A change between two commit contents.
struct EntryChange {
  Entry entry;
  bool deleted;
};

enum class ChangeSource { LOCAL, SYNC };

enum class Status {
  OK,
  PAGE_DELETED,
  NOT_FOUND,
  OBJECT_ID_MISSMATCH,
  IO_ERROR,
  ILLEGAL_STATE,
  NOT_IMPLEMENTED,
};

}  // namespace storage
#endif  // APPS_LEDGER_STORAGE_PUBLIC_TYPES_H_
