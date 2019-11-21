// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_ENCODING_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_ENCODING_H_

#include "src/ledger/bin/storage/impl/object_identifier_generated.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {

// Converts a |ObjectIdentifierStorage| to an |ObjectIdentifier|.
ObjectIdentifier ToObjectIdentifier(const ObjectIdentifierStorage* object_identifier_storage,
                                    ObjectIdentifierFactory* object_identifier_factory);

// Converts a |ObjectIdentifier| to an |ObjectIdentifierStorage|.
flatbuffers::Offset<ObjectIdentifierStorage> ToObjectIdentifierStorage(
    flatbuffers::FlatBufferBuilder* builder, const ObjectIdentifier& object_identifier);

// Encodes an ObjectIdentifier into a string.
std::string EncodeObjectIdentifier(const ObjectIdentifier& object_identifier);

// Decodes an ObjectIdentifier from a string. Returns |true| in case of success,
// |false| otherwise.
bool DecodeObjectIdentifier(absl::string_view data, ObjectIdentifierFactory* factory,
                            ObjectIdentifier* object_identifier);

// Encodes an ObjectIdentifier for a non-inline piece into a fixed-size string whose prefix is the
// serialization of its object digest.
std::string EncodeDigestPrefixedObjectIdentifier(const ObjectIdentifier& object_identifier);

// Decodes an ObjectIdentifier encoded with EncodeDigestPrefixedObjectIdentifier. Returns |true| in
// case of success, |false| otherwise.
bool DecodeDigestPrefixedObjectIdentifier(absl::string_view data, ObjectIdentifierFactory* factory,
                                          ObjectIdentifier* object_identifier);

// Returns whether a |ObjectIdentifierStorage| obtained from flatbuffer is
// valid.
bool IsObjectIdentifierStorageValid(const ObjectIdentifierStorage* storage);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IDENTIFIER_ENCODING_H_
