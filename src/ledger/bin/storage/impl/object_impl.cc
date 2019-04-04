// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_impl.h"

#include <lib/fsl/vmo/strings.h>

#include <utility>

#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

namespace {
uint64_t ToFullPages(uint64_t value) {
  return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
}
}  // namespace

InlinePiece::InlinePiece(ObjectIdentifier identifier)
    : identifier_(std::move(identifier)) {}

fxl::StringView InlinePiece::GetData() const {
  return ExtractObjectDigestData(identifier_.object_digest());
}

ObjectIdentifier InlinePiece::GetIdentifier() const { return identifier_; }

DataChunkPiece::DataChunkPiece(ObjectIdentifier identifier,
                               std::unique_ptr<DataSource::DataChunk> chunk)
    : identifier_(std::move(identifier)), chunk_(std::move(chunk)) {}

fxl::StringView DataChunkPiece::GetData() const { return chunk_->Get(); }

ObjectIdentifier DataChunkPiece::GetIdentifier() const { return identifier_; }

LevelDBPiece::LevelDBPiece(ObjectIdentifier identifier,
                           std::unique_ptr<leveldb::Iterator> iterator)
    : identifier_(std::move(identifier)), iterator_(std::move(iterator)) {}

fxl::StringView LevelDBPiece::GetData() const {
  return convert::ExtendedStringView(iterator_->value());
}

ObjectIdentifier LevelDBPiece::GetIdentifier() const { return identifier_; }

ChunkObject::ChunkObject(std::unique_ptr<const Piece> piece)
    : piece_(std::move(piece)) {
  FXL_DCHECK(
      GetObjectDigestInfo(piece_->GetIdentifier().object_digest()).is_chunk())
      << "INDEX piece " << piece_->GetIdentifier()
      << " cannot be used as an object.";
}

ObjectIdentifier ChunkObject::GetIdentifier() const {
  return piece_->GetIdentifier();
}

Status ChunkObject::GetData(fxl::StringView* data) const {
  *data = piece_->GetData();
  return Status::OK;
}

VmoObject::VmoObject(ObjectIdentifier identifier, fsl::SizedVmo vmo)
    : identifier_(std::move(identifier)), vmo_(std::move(vmo)) {}

VmoObject::~VmoObject() {
  if (vmar_) {
    vmar_.destroy();
  }
}

ObjectIdentifier VmoObject::GetIdentifier() const { return identifier_; }

Status VmoObject::GetData(fxl::StringView* data) const {
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }
  *data = data_;
  return Status::OK;
}

Status VmoObject::GetVmo(fsl::SizedVmo* vmo) const {
  zx_status_t zx_status =
      vmo_.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, vmo);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to duplicate a vmo. Status: " << zx_status;
    return Status::INTERNAL_ERROR;
  }
  return Status::OK;
}

Status VmoObject::Initialize() const {
  if (initialized_) {
    return Status::OK;
  }

  uintptr_t allocate_address;
  zx_status_t zx_status = zx::vmar::root_self()->allocate(
      0, ToFullPages(vmo_.size()),
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, &vmar_,
      &allocate_address);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate VMAR. Error: " << zx_status;
    return Status::INTERNAL_ERROR;
  }

  char* mapped_address;
  zx_status = vmar_.map(0, vmo_.vmo(), 0, vmo_.size(),
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        reinterpret_cast<uintptr_t*>(&mapped_address));
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map VMO. Error: " << zx_status;
    vmar_.reset();
    return Status::INTERNAL_ERROR;
  }

  data_ = fxl::StringView(mapped_address, vmo_.size());
  initialized_ = true;

  return Status::OK;
}

}  // namespace storage
