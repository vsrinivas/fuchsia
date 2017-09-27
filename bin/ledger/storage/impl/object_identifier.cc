// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_identifier.h"

#include <limits>

namespace storage {

namespace {
// The default encryption values. Only used until real encryption is
// implemented: LE-286
//
// Use max_int32 for key_index as it will never be used in practice as it is not
// expected that any user will change its key 2^32 times.
constexpr uint32_t kDefaultKeyIndex = std::numeric_limits<uint32_t>::max();
// Use max_int32 - 1 for default deletion scoped id. max_int32 has a special
// meaning in the specification and is used to have per object deletion scope.
constexpr uint32_t kDefaultDeletionScopeId =
    std::numeric_limits<uint32_t>::max() - 1;
}  // namespace

ObjectIdentifier MakeDefaultObjectIdentifier(ObjectDigest digest) {
  return {kDefaultKeyIndex, kDefaultDeletionScopeId, std::move(digest)};
}

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
