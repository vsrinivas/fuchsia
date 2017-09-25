// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_ID_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_ID_H_

#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

enum class ObjectType {
  VALUE,
  INDEX,
};

enum class ObjectIdType {
  INLINE,
  VALUE_HASH,
  INDEX_HASH,
};

// Returns the type of |object_id|.
ObjectIdType GetObjectIdType(ObjectIdView object_id);

// Returns the object type associated to an object id type.
ObjectType GetObjectType(ObjectIdType id_type);

// Extracts the data from |object_id|. If |object_id| type is |INLINE|, the
// returned data is the content of the object, otherwise, it is the hash of the
// object.
fxl::StringView ExtractObjectIdData(ObjectIdView object_id);

// Computes the id of the object of the given |type| with the given |content|.
ObjectId ComputeObjectId(ObjectType type, convert::ExtendedStringView content);

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_ID_H_
