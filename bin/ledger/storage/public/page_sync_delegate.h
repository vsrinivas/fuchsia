// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_

#include <functional>

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/types.h"

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
                         std::unique_ptr<DataSource::DataChunk>)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSyncDelegate);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_SYNC_DELEGATE_H_
