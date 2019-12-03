// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_JOURNAL_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_JOURNAL_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace storage {

// A |Journal| represents a commit in progress.
class Journal {
 public:
  Journal() = default;
  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;
  virtual ~Journal() = default;

  // Adds an entry with the given |key| and |object_identifier| to this
  // |Journal|.
  virtual void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
                   KeyPriority priority) = 0;

  // Deletes the entry with the given |key| from this |Journal|.
  virtual void Delete(convert::ExtendedStringView key) = 0;

  // Deletes all entries from this Journal, as well as any entries already
  // present on the page. This doesn't prevent subsequent calls to update the
  // contents of this Journal (|Put|, |Delete| or |Clear|).
  virtual void Clear() = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_JOURNAL_H_
