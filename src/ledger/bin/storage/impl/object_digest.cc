// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_digest.h"

#include <bitset>

#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/constants.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

namespace {

static_assert(kStorageHashSize == encryption::kHashSize, "Unexpected kStorageHashSize value");

// The first bit is 1 for inlined values and 0 otherwise.
constexpr size_t kInlineBit = 0;

// The second bit is 0 for CHUNK and 1 for INDEX.
constexpr size_t kTypeBit = 1;

// The third bit is 1 for a tree node and 0 otherwise.
constexpr char kTreeNodeBit = 2;

// Builds an object digest by concatenating |prefix| and |data|.
ObjectDigest BuildDigest(std::bitset<8> prefix, convert::ExtendedStringView data) {
  std::string result;
  result.reserve(data.size() + 1);
  result.push_back(prefix.to_ulong());
  result.append(data.data(), data.size());
  return ObjectDigest(std::move(result));
}

}  // namespace

bool IsDigestValid(convert::ExtendedStringView object_digest) {
  // All object digests should have a prefix.
  if (object_digest.size() == 0) {
    FXL_LOG(INFO) << "Invalid object digest: empty.";
    return false;
  }
  std::bitset<8> prefix(object_digest[0]);
  // Check inline bit.
  if (prefix[kInlineBit] && object_digest.size() > kStorageHashSize + 1) {
    FXL_LOG(INFO) << "Invalid object digest: inline but size=" << object_digest.size()
                  << "; digest=" << convert::ToHex(object_digest);
    return false;
  }
  if (!prefix[kInlineBit] && object_digest.size() != kStorageHashSize + 1) {
    FXL_LOG(INFO) << "Invalid object digest: not inline but size=" << object_digest.size()
                  << "; digest=" << convert::ToHex(object_digest);
    return false;
  }
  // All bits must be zero except the ones we use for ObjectDigestInfo.
  prefix.reset(kInlineBit);
  prefix.reset(kTypeBit);
  prefix.reset(kTreeNodeBit);
  return prefix.none();
}

bool IsDigestValid(const ObjectDigest& object_digest) {
  return object_digest.IsValid() && IsDigestValid(object_digest.Serialize());
}

ObjectDigestInfo GetObjectDigestInfo(const ObjectDigest& object_digest) {
  FXL_DCHECK(IsDigestValid(object_digest));

  const std::string& digest = object_digest.Serialize();
  std::bitset<8> prefix(digest[0]);
  ObjectDigestInfo result;
  result.object_type = prefix[kTreeNodeBit] ? ObjectType::TREE_NODE : ObjectType::BLOB;
  result.piece_type = prefix[kTypeBit] ? PieceType::INDEX : PieceType::CHUNK;
  result.inlined = prefix[kInlineBit] ? InlinedPiece::YES : InlinedPiece::NO;
  return result;
}

absl::string_view ExtractObjectDigestData(const ObjectDigest& object_digest) {
  FXL_DCHECK(IsDigestValid(object_digest));

  absl::string_view digest = object_digest.Serialize();
  return digest.substr(1);
}

ObjectDigest ComputeObjectDigest(PieceType piece_type, ObjectType object_type,
                                 convert::ExtendedStringView content) {
  std::bitset<8> prefix;
  prefix[kTypeBit] = piece_type == PieceType::INDEX;
  prefix[kTreeNodeBit] = object_type == ObjectType::TREE_NODE;
  if (content.size() <= kStorageHashSize) {
    prefix[kInlineBit] = true;
    return BuildDigest(prefix, content);
  }
  return BuildDigest(prefix, encryption::SHA256WithLengthHash(content));
}

}  // namespace storage
