// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_JOURNAL_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_JOURNAL_H_

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/macros.h"

namespace storage {

// A |Journal| represents a commit in progress.
class Journal {
 public:
  Journal() {}
  virtual ~Journal() {}

  // Adds an entry with the given |key| and |object_id| to this |Journal|.
  // Returns |OK| on success or the error code otherwise.
  virtual Status Put(convert::ExtendedStringView key,
                     ObjectIdView object_id,
                     KeyPriority priority) = 0;

  // Deletes the entry with the given |key| from this |Journal|. Returns |OK|
  // on success or the error code otherwise.
  virtual Status Delete(convert::ExtendedStringView key) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Journal);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_JOURNAL_H_
