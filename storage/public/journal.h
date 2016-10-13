// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_JOURNAL_H_
#define APPS_LEDGER_STORAGE_PUBLIC_JOURNAL_H_

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

// A |Journal| represents a commit in progress.
class Journal {
 public:
  Journal() {}
  virtual ~Journal() {}

  // Adds an entry with the given |key| and |blob_id| to this |Journal|. Returns
  // |OK| on success or the error code otherwise.
  virtual Status Put(convert::ExtendedStringView key,
                     ObjectIdView blob_id,
                     KeyPriority priority) = 0;

  // Deletes the entry with the given |key| from this |Journal|. Returns |OK|
  // on success or the error code otherwise.
  virtual Status Delete(convert::ExtendedStringView key) = 0;

  // Commits the changes of this |Journal|. Trying to update entries or rollback
  // will fail after a successful commit. The id of the created commit is
  // returned in |commit_id|.
  virtual Status Commit(CommitId* commit_id) = 0;

  // Rolls back all changes to this |Journal|. Trying to update entries or
  // commit will fail with an |ILLEGAL_STATE| after a successful rollback.
  virtual Status Rollback() = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Journal);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_JOURNAL_H_
