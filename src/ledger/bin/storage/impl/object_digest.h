// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_DIGEST_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_DIGEST_H_

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// The two types of pieces. When an object is split into multiple pieces, it
// produces a tree made of:
// - chunks, at the leaves, that hold the actual values to be concatenated to
// reconstruct the object,
// - indices, that reference other pieces.
// Note that an object that is small enough might not need to be split into
// multiple pieces (see split.h for the splitting algorithm). In that case, the
// tree of pieces degenerates to the simple case of single chunk encoding the
// whole object.
enum class PieceType {
  CHUNK,
  INDEX,
};

// Whether the piece is stored inline. Inlined pieces are small-enough pieces
// that are embedded directly in the |ObjectDigest| representing them. If a
// piece is not inlined, it is stored as a separate blob object, and is
// referenced by the |ObjectDigest|, which is a hash of its content.
enum class InlinedPiece { NO, YES };

// Details about the piece represented by an |ObjectDigest|.
// This information is encoded in the first byte of the digest.
struct ObjectDigestInfo {
  // The type of the object encoded by the piece. |object_type| is |TREE_NODE|,
  // if this piece refers to a tree node object that was not split into pieces,
  // or if it refers to the root-index of a chunked tree node object; |BLOB|
  // otherwise.
  // Consequently, there is no way to distinguish between a piece encoding a
  // blob object, and an internal piece of a split tree node; deduplication even
  // means that a single piece may represent both depending on context.
  ObjectType object_type;

  // The type of the piece.
  PieceType piece_type;

  // Whether the piece is stored inline.
  InlinedPiece inlined;

  bool is_inlined() const { return inlined == InlinedPiece::YES; }
  bool is_chunk() const { return piece_type == PieceType::CHUNK; }
};

// Returns whether the given digest is valid.
bool IsDigestValid(convert::ExtendedStringView object_digest);

// Returns whether the given digest is valid.
bool IsDigestValid(const ObjectDigest& object_digest);

// Returns the type of |object_digest|.
ObjectDigestInfo GetObjectDigestInfo(const ObjectDigest& object_digest);

// Extracts the data from |object_digest|. If |object_digest| type is |INLINE|,
// the returned data is the content of the object, otherwise, it is the hash of
// the object. The returned view is valid for as long as |object digest|.
absl::string_view ExtractObjectDigestData(const ObjectDigest& object_digest);

// Computes the id of a piece with the given |piece_type|, |object_type| and
// |content|. The inlined bit of ObjectDigestInfo does not need to be provided
// because it is derived from |content|'s length.
ObjectDigest ComputeObjectDigest(PieceType piece_type, ObjectType object_type,
                                 convert::ExtendedStringView content);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_DIGEST_H_
