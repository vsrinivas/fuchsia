// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_READ_DATA_SOURCE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_READ_DATA_SOURCE_H_

#include <memory>

#include <lib/fit/function.h>

#include "lib/callback/managed_container.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// Reads the given data source, and returns a single data chunk containing its
// content. This method will not call its |callback| if |managed_container| is
// deleted.
void ReadDataSource(
    callback::ManagedContainer* managed_container,
    std::unique_ptr<DataSource> data_source,
    fit::function<void(Status, std::unique_ptr<DataSource::DataChunk>)>
        callback);
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_READ_DATA_SOURCE_H_
