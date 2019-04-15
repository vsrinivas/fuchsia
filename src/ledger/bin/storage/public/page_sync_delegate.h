// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/macros.h"

namespace storage {

// Delegate interface for PageStorage responsible for retrieving on-demand
// storage objects from the cloud.
class PageSyncDelegate {
 public:
  PageSyncDelegate() {}
  virtual ~PageSyncDelegate() {}

  // Retrieves the object of the given id from the cloud.
  virtual void GetObject(
      ObjectIdentifier object_identifier,
      fit::function<void(Status status, ChangeSource source,
                         IsObjectSynced is_object_synced,
                         std::unique_ptr<DataSource::DataChunk>)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSyncDelegate);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
