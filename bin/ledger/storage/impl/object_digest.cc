// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_digest.h"

#include <lib/fxl/strings/concatenate.h>

#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/impl/constants.h"

namespace storage {

namespace {

static_assert(kStorageHashSize == encryption::kHashSize,
              "Unexpected kStorageHashSize value");

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

bool IsDigestValid(convert::ExtendedStringView object_digest) {
  if (object_digest.size() <= kStorageHashSize) {
    // |object_digest| is of type inline.
    return true;
  }
  if (object_digest.size() != kStorageHashSize + 1) {
    // Digest size is invalid.
    return false;
  }
  // The first character of the digest indicates its type.
  switch (object_digest[0]) {
    case kValueHashPrefix:
    case kIndexHashPrefix:
      return true;
  }
  // The first character is invalid.
  return false;
}

bool IsDigestValid(const ObjectDigest& object_digest) {
  return IsDigestValid(object_digest.Serialize());
}

ObjectDigestType GetObjectDigestType(const ObjectDigest& object_digest) {
  FXL_DCHECK(IsDigestValid(object_digest));

  const std::string& digest = object_digest.Serialize();

  if (digest.size() <= kStorageHashSize) {
    return ObjectDigestType::INLINE;
  }

  switch (digest[0]) {
    case kValueHashPrefix:
      return ObjectDigestType::CHUNK_HASH;
    case kIndexHashPrefix:
      return ObjectDigestType::INDEX_HASH;
  }

  FXL_NOTREACHED() << "Unknown digest prefix: "
                   << static_cast<uint32_t>(digest[0]);
  return ObjectDigestType::CHUNK_HASH;
}

ObjectType GetObjectType(ObjectDigestType digest_type) {
  switch (digest_type) {
    case ObjectDigestType::INLINE:
    case ObjectDigestType::CHUNK_HASH:
      return ObjectType::CHUNK;
    case ObjectDigestType::INDEX_HASH:
      return ObjectType::INDEX;
  }
}

fxl::StringView ExtractObjectDigestData(const ObjectDigest& object_digest) {
  FXL_DCHECK(IsDigestValid(object_digest));

  fxl::StringView digest = object_digest.Serialize();

  if (digest.size() <= kStorageHashSize) {
    return digest;
  }

  FXL_DCHECK(digest[0] == kValueHashPrefix || digest[0] == kIndexHashPrefix);

  return digest.substr(1);
}

ObjectDigest ComputeObjectDigest(ObjectType type,
                                 convert::ExtendedStringView content) {
  switch (type) {
    case ObjectType::CHUNK:
      if (content.size() <= kStorageHashSize) {
        return ObjectDigest(content.ToString());
      }
      return ObjectDigest(AddPrefix(kValueHashPrefix,
                                    encryption::SHA256WithLengthHash(content)));
    case ObjectType::INDEX:
      return ObjectDigest(AddPrefix(kIndexHashPrefix,
                                    encryption::SHA256WithLengthHash(content)));
  }
}

}  // namespace storage
