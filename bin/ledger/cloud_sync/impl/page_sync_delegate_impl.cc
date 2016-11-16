// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_delegate_impl.h"

namespace cloud_sync {

PageSyncDelegateImpl::PageSyncDelegateImpl(storage::PageStorage* storage)
    : storage_(storage) {
  storage_->SetSyncDelegate(this);
}

PageSyncDelegateImpl::~PageSyncDelegateImpl() {
  storage_->SetSyncDelegate(nullptr);
}

void PageSyncDelegateImpl::GetObject(
    storage::ObjectIdView object_id,
    std::function<void(storage::Status status,
                       uint64_t size,
                       mx::datapipe_consumer data)> callback) {
  callback(storage::Status::NOT_IMPLEMENTED, 0u, mx::datapipe_consumer());
}

}  // namespace cloud_sync
