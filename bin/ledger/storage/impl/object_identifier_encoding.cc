// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"

#include <limits>

#include "peridot/bin/ledger/storage/impl/object_digest.h"

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

std::string EncodeObjectIdentifier(const ObjectIdentifier& object_identifier) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(ToObjectIdentifierStorage(&builder, object_identifier));
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                     builder.GetSize());
}

bool DecodeObjectIdentifier(fxl::StringView data,
                            ObjectIdentifier* object_identifier) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyObjectIdentifierStorageBuffer(verifier)) {
    return false;
  }
  const ObjectIdentifierStorage* storage = GetObjectIdentifierStorage(
      reinterpret_cast<const unsigned char*>(data.data()));
  if (!IsDigestValid(storage->object_digest())) {
    return false;
  }
  *object_identifier = ToObjectIdentifier(storage);
  return true;
}

}  // namespace storage
