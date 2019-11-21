// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_SPLIT_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_SPLIT_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// Status for the |SplitDataSource| and |CollectPieces| callbacks.
enum class IterationStatus {
  DONE,
  IN_PROGRESS,
  ERROR,
};

// Splits the data from |source| representing an object of some |type| and
// builds a multi-level index from the content. The |source| is consumed and
// split using a rolling hash. Each chunk and each index file is returned. On
// each iteration, |make_object_identifier| is called first and must return the
// |ObjectIdentifier| to use to reference the given content id. The piece is
// then passed to |callback|, along with a status of |IN_PROGRESS|, except for
// the last piece which has a status of |DONE|.
// See split.md for information about the algorithm.
void SplitDataSource(DataSource* source, ObjectType type,
                     fit::function<ObjectIdentifier(ObjectDigest)> make_object_identifier,
                     fit::function<uint64_t(uint64_t)> chunk_permutation,
                     fit::function<void(IterationStatus, std::unique_ptr<Piece>)> callback);

// Iterates over the children of an index object.
Status ForEachIndexChild(absl::string_view index_content, ObjectIdentifierFactory* factory,
                         fit::function<Status(ObjectIdentifier)> callback);

// Collects all pieces ids needed to build the object with id |root|. This
// returns the id of the object itself, and recurse inside any index if the
// |callback| returned true for the given id.
void CollectPieces(
    ObjectIdentifier root,
    fit::function<void(ObjectIdentifier, fit::function<void(Status, absl::string_view)>)>
        data_accessor,
    fit::function<bool(IterationStatus, ObjectIdentifier)> callback);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_SPLIT_H_
