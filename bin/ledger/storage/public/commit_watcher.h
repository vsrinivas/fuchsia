// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_WATCHER_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_WATCHER_H_

#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {

class CommitWatcher {
 public:
  CommitWatcher() {}
  virtual ~CommitWatcher() {}

  // Called when a new commit has been created.
  virtual void OnNewCommit(const Commit& commit, ChangeSource source) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitWatcher);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_WATCHER_H_
