// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_

#include <unordered_set>

#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// Status for the |SplitDataSource| and |CollectXXXPieces| callbacks.
enum class IterationStatus {
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
    std::function<void(IterationStatus,
                       ObjectId,
                       std::unique_ptr<DataSource::DataChunk>)> callback);

// Recurse over all pieces of an index object.
Status ForEachPiece(fxl::StringView index_content,
                    std::function<Status(ObjectIdView)> callback);

// Collects all pieces ids needed to build the object with id |root|. This
// returns the id of the object itself, and recurse inside any index if the
// |callback| returned true for the given id.
void CollectPieces(
    ObjectIdView root,
    std::function<void(ObjectIdView,
                       std::function<void(Status, fxl::StringView)>)>
        data_accessor,
    std::function<bool(IterationStatus, ObjectIdView)> callback);

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_SPLIT_H_
