// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_

#include <functional>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace cloud_sync {

// Manages cloud sync for a single page.
//
// PageSync is responsible for uploading locally created artifacts (commits and
// objects) of the page from storage to the cloud, and for fetching remote
// artifacts of the same page from the cloud and putting them in storage.
class PageSync {
 public:
  PageSync(){};
  virtual ~PageSync(){};

  // Starts syncing. Upon connection drop, the sync will restart automatically,
  // the client doesn't need to call Start() again.
  virtual void Start() = 0;

  // Sets a callback that will be called after Start() every time when PageSync
  // becomes idle, that is: finished uploading alll unsynced local artifacts to
  // the cloud and not downloading any remote artifacts.
  virtual void SetOnIdle(ftl::Closure on_idle_callback) = 0;

  // Returns true iff PageSync is idle, that is with no pending upload or
  // download work.
  virtual bool IsIdle() = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageSync);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_PUBLIC_PAGE_SYNC_H_
