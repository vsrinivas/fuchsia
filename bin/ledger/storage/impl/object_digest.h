// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_DIGEST_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_DIGEST_H_

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace storage {

enum class ObjectType {
  VALUE,
  INDEX,
};

enum class ObjectDigestType {
  INLINE,
  VALUE_HASH,
  INDEX_HASH,
};

// Returns whether the given digest is valid.
bool IsDigestValid(ObjectDigestView object_digest);

// Returns the type of |object_digest|.
ObjectDigestType GetObjectDigestType(ObjectDigestView object_digest);

// Returns the object type associated to an object id type.
ObjectType GetObjectType(ObjectDigestType digest_type);

// Extracts the data from |object_digest|. If |object_digest| type is |INLINE|,
// the returned data is the content of the object, otherwise, it is the hash of
// the object.
fxl::StringView ExtractObjectDigestData(ObjectDigestView object_digest);

// Computes the id of the object of the given |type| with the given |content|.
ObjectDigest ComputeObjectDigest(ObjectType type,
                                 convert::ExtendedStringView content);

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_DIGEST_H_
