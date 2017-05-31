// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_

#include "apps/ledger/src/storage/public/data_source.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {

// Status for the |SplitDataSource| callback.
enum class SplitStatus {
  DONE,
  IN_PROGRESS,
  ERROR,
};

// Splits the data from |source| and builds a multi-level index from the
// content. The |source| is consumed and split using a rolling hash. Each chunk
// and each index file is returned via |callback| with a status of
// |IN_PROGRESS|, the id of the content, and the content itself. Then the last
// call of |callback| is done with a status of |DONE|, the final id for the data
// and a |nullptr| chunk. |callback| is not called anymore once |source| is
// deleted.
void SplitDataSource(
    DataSource* source,
    std::function<void(SplitStatus,
                       ObjectId,
                       std::unique_ptr<DataSource::DataChunk>)> callback);
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_
