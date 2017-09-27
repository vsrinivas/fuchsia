// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IDENTIFIER_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IDENTIFIER_H_

#include "peridot/bin/ledger/storage/impl/object_identifier_generated.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {

// Creates an |ObjectIdentifier| from an |ObjectDigest|.
//
// TODO(qsr): This is only used until LE-286 (real encryption) is implemented.
ObjectIdentifier MakeDefaultObjectIdentifier(ObjectDigest digest);

// Converts a |ObjectIdentifierStorage| to an |ObjectIdentifier|.
ObjectIdentifier ToObjectIdentifier(
    const ObjectIdentifierStorage* object_identifier_storage);

// Converts a |ObjectIdentifier| to an |ObjectIdentifierStorage|.
flatbuffers::Offset<ObjectIdentifierStorage> ToObjectIdentifierStorage(
    flatbuffers::FlatBufferBuilder* builder,
    const ObjectIdentifier& object_identifier);

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IDENTIFIER_H_
