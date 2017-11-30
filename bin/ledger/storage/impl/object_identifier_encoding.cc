// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"

#include <limits>

namespace storage {

ObjectIdentifier ToObjectIdentifier(
    const ObjectIdentifierStorage* object_identifier_storage) {
  return {object_identifier_storage->key_index(),
          object_identifier_storage->deletion_scope_id(),
          convert::ToString(object_identifier_storage->object_digest())};
}

flatbuffers::Offset<ObjectIdentifierStorage> ToObjectIdentifierStorage(
    flatbuffers::FlatBufferBuilder* builder,
    const ObjectIdentifier& object_identifier) {
  return CreateObjectIdentifierStorage(
      *builder, object_identifier.key_index,
      object_identifier.deletion_scope_id,
      convert::ToFlatBufferVector(builder, object_identifier.object_digest));
}

}  // namespace storage
