// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_id.h"

#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/glue/crypto/hash.h"
#include "peridot/bin/ledger/storage/impl/constants.h"

namespace storage {

namespace {

constexpr char kValueHashPrefix = 1;
constexpr char kIndexHashPrefix = 2;

std::string AddPrefix(char c, convert::ExtendedStringView data) {
  std::string result;
  result.reserve(data.size() + 1);
  result.push_back(c);
  result.append(data.data(), data.size());
  return result;
}

}  // namespace

ObjectIdType GetObjectIdType(ObjectIdView object_id) {
  if (object_id.size() <= kStorageHashSize) {
    return ObjectIdType::INLINE;
  }

  switch (object_id[0]) {
    case kValueHashPrefix:
      return ObjectIdType::VALUE_HASH;
    case kIndexHashPrefix:
      return ObjectIdType::INDEX_HASH;
  }

  FXL_NOTREACHED();
  return ObjectIdType::VALUE_HASH;
}

ObjectType GetObjectType(ObjectIdType id_type) {
  switch (id_type) {
    case ObjectIdType::INLINE:
    case ObjectIdType::VALUE_HASH:
      return ObjectType::VALUE;
    case ObjectIdType::INDEX_HASH:
      return ObjectType::INDEX;
  }
}

fxl::StringView ExtractObjectIdData(ObjectIdView object_id) {
  if (object_id.size() <= kStorageHashSize) {
    return object_id;
  }

  FXL_DCHECK(object_id[0] == kValueHashPrefix ||
             object_id[0] == kIndexHashPrefix);

  return object_id.substr(1);
}

ObjectId ComputeObjectId(ObjectType type, convert::ExtendedStringView content) {
  switch (type) {
    case ObjectType::VALUE:
      if (content.size() <= kStorageHashSize) {
        return content.ToString();
      }
      return AddPrefix(kValueHashPrefix, glue::SHA256Hash(content));
    case ObjectType::INDEX:
      return AddPrefix(kIndexHashPrefix, glue::SHA256Hash(content));
  }
}

}  // namespace storage
