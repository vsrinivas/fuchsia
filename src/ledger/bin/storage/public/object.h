// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_OBJECT_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_OBJECT_H_

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {

// An object is a potentially large piece of data, identified by an object
// identifier. Accessing an object can fail, for instance because its data may
// be mapped into memory on-demand.
class Object {
 public:
  Object() = default;
  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;
  virtual ~Object() = default;

  // Returns the identifier of this storage object.
  virtual ObjectIdentifier GetIdentifier() const = 0;

  // Returns the data of this object. The returned view is valid as long as this
  // object is not deleted.
  virtual Status GetData(fxl::StringView* data) const = 0;

  // Returns a vmo containing the data.
  virtual Status GetVmo(fsl::SizedVmo* vmo) const;

  // Adds tree-level references from this object to other objects into
  // |references|. Does not clear |references|. Does not add piece-level
  // references (use |Piece::AppendReferences| instead).
  virtual Status AppendReferences(ObjectReferencesAndPriority* references) const = 0;
};

// A piece is a chunk of data small enough that accessing it never fails.
// Objects are higher-level blobs of data and may be split into a number of
// pieces before being stored in Ledger.
class Piece {
 public:
  Piece() = default;
  Piece(const Piece&) = delete;
  Piece& operator=(const Piece&) = delete;
  virtual ~Piece() = default;

  // Returns the identifier of this storage object.
  virtual ObjectIdentifier GetIdentifier() const = 0;

  // Returns the data of this piece. The returned view is valid as long as this
  // piece is not deleted.
  virtual fxl::StringView GetData() const = 0;

  // Adds piece-level references from this piece to other pieces into
  // |references|. Does not clear |references|. Does not add tree-level
  // references (use |Object::AppendReferences| instead).
  virtual Status AppendReferences(ObjectReferencesAndPriority* references) const = 0;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_OBJECT_H_
