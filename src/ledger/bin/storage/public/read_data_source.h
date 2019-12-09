// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_READ_DATA_SOURCE_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_READ_DATA_SOURCE_H_

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/managed_container.h"

namespace storage {

// Reads the given data source, and returns a single data chunk containing its
// content. This method will not call its |callback| if |managed_container| is
// deleted.
void ReadDataSource(ledger::ManagedContainer* managed_container,
                    std::unique_ptr<DataSource> data_source,
                    fit::function<void(Status, std::unique_ptr<DataSource::DataChunk>)> callback);
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_READ_DATA_SOURCE_H_
