// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_DELEGATE_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_DELEGATE_IMPL_H_

#include "apps/ledger/src/storage/public/page_sync_delegate.h"

#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"

namespace cloud_sync {

class PageSyncDelegateImpl : public storage::PageSyncDelegate {
 public:
  PageSyncDelegateImpl(storage::PageStorage* storage);
  virtual ~PageSyncDelegateImpl();

  void GetObject(
      storage::ObjectIdView object_id,
      std::function<void(storage::Status status,
                         uint64_t size,
                         mx::datapipe_consumer data)> callback) override;

 private:
  storage::PageStorage* storage_;
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_IMPL_PAGE_SYNC_DELEGATE_IMPL_H_
