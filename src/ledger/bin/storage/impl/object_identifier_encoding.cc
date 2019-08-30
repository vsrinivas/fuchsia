// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"

#include <limits>

#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {
ObjectIdentifier ToObjectIdentifier(const ObjectIdentifierStorage* object_identifier_storage,
                                    ObjectIdentifierFactory* object_identifier_factory) {
  return object_identifier_factory->MakeObjectIdentifier(
      object_identifier_storage->key_index(),
      ObjectDigest(object_identifier_storage->object_digest()));
}

flatbuffers::Offset<ObjectIdentifierStorage> ToObjectIdentifierStorage(
    flatbuffers::FlatBufferBuilder* builder, const ObjectIdentifier& object_identifier) {
  return CreateObjectIdentifierStorage(
      *builder, object_identifier.key_index(),
      convert::ToFlatBufferVector(builder, object_identifier.object_digest().Serialize()));
}

// This encoding is used:
// - To name objects in the database. This relies on the encoding of an object identifier always
//   being the same sequence of bytes.
// - To store in entry payloads in cloud_sync. This requires us to be able to read back old object
//   identifiers.
// - To generate entry ids for merges. If two different encodings can be produced, this will cause
//   some merges to be duplicated, but they will stay correct.
std::string EncodeObjectIdentifier(const ObjectIdentifier& object_identifier) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(ToObjectIdentifierStorage(&builder, object_identifier));
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
}

bool DecodeObjectIdentifier(fxl::StringView data, ObjectIdentifierFactory* factory,
                            ObjectIdentifier* object_identifier) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyObjectIdentifierStorageBuffer(verifier)) {
    return false;
  }
  const ObjectIdentifierStorage* storage =
      GetObjectIdentifierStorage(reinterpret_cast<const unsigned char*>(data.data()));
  if (!IsObjectIdentifierStorageValid(storage)) {
    return false;
  }
  *object_identifier = ToObjectIdentifier(storage, factory);
  return true;
}

bool IsObjectIdentifierStorageValid(const ObjectIdentifierStorage* storage) {
  return storage && storage->object_digest();
}

}  // namespace storage
