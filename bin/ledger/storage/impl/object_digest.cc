// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_digest.h"

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

ObjectDigestType GetObjectDigestType(ObjectDigestView object_digest) {
  if (object_digest.size() <= kStorageHashSize) {
    return ObjectDigestType::INLINE;
  }

  switch (object_digest[0]) {
    case kValueHashPrefix:
      return ObjectDigestType::VALUE_HASH;
    case kIndexHashPrefix:
      return ObjectDigestType::INDEX_HASH;
  }

  FXL_NOTREACHED();
  return ObjectDigestType::VALUE_HASH;
}

ObjectType GetObjectType(ObjectDigestType digest_type) {
  switch (digest_type) {
    case ObjectDigestType::INLINE:
    case ObjectDigestType::VALUE_HASH:
      return ObjectType::VALUE;
    case ObjectDigestType::INDEX_HASH:
      return ObjectType::INDEX;
  }
}

fxl::StringView ExtractObjectDigestData(ObjectDigestView object_digest) {
  if (object_digest.size() <= kStorageHashSize) {
    return object_digest;
  }

  FXL_DCHECK(object_digest[0] == kValueHashPrefix ||
             object_digest[0] == kIndexHashPrefix);

  return object_digest.substr(1);
}

ObjectDigest ComputeObjectDigest(ObjectType type,
                                 convert::ExtendedStringView content) {
  switch (type) {
    case ObjectType::VALUE:
      if (content.size() <= kStorageHashSize) {
        return content.ToString();
      }
      return AddPrefix(kValueHashPrefix, glue::SHA256WithLengthHash(content));
    case ObjectType::INDEX:
      return AddPrefix(kIndexHashPrefix, glue::SHA256WithLengthHash(content));
  }
}

}  // namespace storage
