// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

// This file contains methods to serialize and deserialize CommitIds to and from flatbuffers.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_SERIALIZATION_H_

#include "src/ledger/bin/storage/impl/commit_generated.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
// Converts a CommitIdView into a flatbuffer IdStorage. The return value is valid as long as the
// data backing |id| is valid.
const IdStorage* ToIdStorage(CommitIdView id);

// Converts a flatbuffer IdStorage into a CommitIdView. The view is valid as long as the flatbuffer
// is valid.
CommitIdView ToCommitIdView(const IdStorage* fb_id_storage);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_SERIALIZATION_H_
