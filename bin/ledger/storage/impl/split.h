// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_SPLIT_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_SPLIT_H_

#include <lib/fit/function.h>

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
// deleted. On each iteration, |callback| must return the |ObjectIdentifier| to
// use to reference the given content.
void SplitDataSource(
    DataSource* source,
    fit::function<ObjectIdentifier(IterationStatus, ObjectDigest,
                                   std::unique_ptr<DataSource::DataChunk>)>
        callback);

// Recurse over all pieces of an index object.
Status ForEachPiece(fxl::StringView index_content,
                    fit::function<Status(ObjectIdentifier)> callback);

// Collects all pieces ids needed to build the object with id |root|. This
// returns the id of the object itself, and recurse inside any index if the
// |callback| returned true for the given id.
void CollectPieces(
    ObjectIdentifier root,
    fit::function<void(ObjectIdentifier,
                       fit::function<void(Status, fxl::StringView)>)>
        data_accessor,
    fit::function<bool(IterationStatus, ObjectIdentifier)> callback);

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_SPLIT_H_
