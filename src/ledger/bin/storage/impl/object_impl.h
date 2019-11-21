// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IMPL_H_

#include <lib/zx/vmar.h>

#include <memory>

#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/strings/string_view.h"
#include "third_party/leveldb/include/leveldb/iterator.h"

namespace storage {
// Common methods shared by all piece implementations.
class BasePiece : public Piece {
 public:
  Status AppendReferences(ObjectReferencesAndPriority* references) const override;
};

// Piece whose data is equal to its id.
class InlinePiece : public BasePiece {
 public:
  explicit InlinePiece(ObjectIdentifier identifier);

  // Piece:
  fxl::StringView GetData() const override;
  ObjectIdentifier GetIdentifier() const override;

 private:
  const ObjectIdentifier identifier_;
};

// Piece whose data is backed by a DataChunk.
class DataChunkPiece : public BasePiece {
 public:
  explicit DataChunkPiece(ObjectIdentifier identifier,
                          std::unique_ptr<DataSource::DataChunk> chunk);

  // Piece:
  fxl::StringView GetData() const override;
  ObjectIdentifier GetIdentifier() const override;

 private:
  const ObjectIdentifier identifier_;
  std::unique_ptr<DataSource::DataChunk> chunk_;
};

// Piece whose data is backed by a value in LevelDB.
class LevelDBPiece : public BasePiece {
 public:
  explicit LevelDBPiece(ObjectIdentifier identifier, std::unique_ptr<leveldb::Iterator> iterator);

  // Piece:
  fxl::StringView GetData() const override;
  ObjectIdentifier GetIdentifier() const override;

 private:
  const ObjectIdentifier identifier_;
  std::unique_ptr<leveldb::Iterator> iterator_;
};

// Common methods shared by all object implementations.
class BaseObject : public Object {
 public:
  Status AppendReferences(ObjectReferencesAndPriority* references) const override;
};

// Object whose data is backed by a single chunk piece.
class ChunkObject : public BaseObject {
 public:
  // |piece| must be of type CHUNK; index pieces cannot be turned into objects
  // automatically.
  explicit ChunkObject(std::unique_ptr<const Piece> piece);

  // Returns the |piece| backing this object. This object must not be used
  // anymore once this function has returned.
  std::unique_ptr<const Piece> ReleasePiece();

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  std::unique_ptr<const Piece> piece_;
};

// Object whose data is backed by a VMO.
class VmoObject : public BaseObject {
 public:
  VmoObject(ObjectIdentifier identifier, fsl::SizedVmo vmo);
  ~VmoObject() override;

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;
  Status GetVmo(fsl::SizedVmo* vmo) const override;

 private:
  Status Initialize() const;

  mutable bool initialized_ = false;
  const ObjectIdentifier identifier_;
  fsl::SizedVmo vmo_;
  mutable zx::vmar vmar_;
  mutable fxl::StringView data_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_OBJECT_IMPL_H_
