// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_JOURNAL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_JOURNAL_H_

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace storage {

// A |Journal| represents a commit in progress.
class Journal {
 public:
  Journal() {}
  virtual ~Journal() {}

  // Returns the ID of this journal.
  virtual const JournalId& GetId() const = 0;

  // Adds an entry with the given |key| and |object_identifier| to this
  // |Journal|. Returns |OK| on success or the error code otherwise.
  virtual void Put(convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier, KeyPriority priority,
                   fit::function<void(Status)> callback) = 0;

  // Deletes the entry with the given |key| from this |Journal|. Returns |OK|
  // on success or the error code otherwise.
  virtual void Delete(convert::ExtendedStringView key,
                      fit::function<void(Status)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Journal);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_JOURNAL_H_
