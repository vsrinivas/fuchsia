// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_impl.h"

#include <zircon/status.h>

#include <utility>

#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/file_index.h"
#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

namespace {
uint64_t ToFullPages(uint64_t value) { return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)); }
}  // namespace

Status BasePiece::AppendReferences(ObjectReferencesAndPriority* references) const {
  // Chunks have no references.
  const auto digest_info = GetObjectDigestInfo(GetIdentifier().object_digest());
  if (digest_info.is_chunk()) {
    return Status::OK;
  }
  FXL_DCHECK(digest_info.piece_type == PieceType::INDEX);
  // The piece is an index: parse it and append its children to references.
  const FileIndex* file_index;
  RETURN_ON_ERROR(FileIndexSerialization::ParseFileIndex(GetData(), &file_index));
  for (const auto* child : *file_index->children()) {
    ObjectDigest child_digest =
        ToObjectIdentifier(child->object_identifier(), GetIdentifier().factory()).object_digest();
    // References must not contain inline pieces.
    if (GetObjectDigestInfo(child_digest).is_inlined()) {
      continue;
    }
    // Piece references are always eager.
    references->emplace(child_digest, KeyPriority::EAGER);
  }
  return Status::OK;
}

InlinePiece::InlinePiece(ObjectIdentifier identifier) : identifier_(std::move(identifier)) {}

absl::string_view InlinePiece::GetData() const {
  return ExtractObjectDigestData(identifier_.object_digest());
}

ObjectIdentifier InlinePiece::GetIdentifier() const { return identifier_; }

DataChunkPiece::DataChunkPiece(ObjectIdentifier identifier,
                               std::unique_ptr<DataSource::DataChunk> chunk)
    : identifier_(std::move(identifier)), chunk_(std::move(chunk)) {}

absl::string_view DataChunkPiece::GetData() const { return chunk_->Get(); }

ObjectIdentifier DataChunkPiece::GetIdentifier() const { return identifier_; }

LevelDBPiece::LevelDBPiece(ObjectIdentifier identifier, std::unique_ptr<leveldb::Iterator> iterator)
    : identifier_(std::move(identifier)), iterator_(std::move(iterator)) {}

absl::string_view LevelDBPiece::GetData() const {
  return convert::ExtendedStringView(iterator_->value());
}

ObjectIdentifier LevelDBPiece::GetIdentifier() const { return identifier_; }

Status BaseObject::AppendReferences(ObjectReferencesAndPriority* references) const {
  FXL_DCHECK(references);
  // Blobs have no references.
  const auto digest_info = GetObjectDigestInfo(GetIdentifier().object_digest());
  if (digest_info.object_type == ObjectType::BLOB) {
    return Status::OK;
  }
  FXL_DCHECK(digest_info.object_type == ObjectType::TREE_NODE);
  // Parse the object into a TreeNode.
  std::unique_ptr<const btree::TreeNode> node;
  RETURN_ON_ERROR(btree::TreeNode::FromObject(*this, &node));
  node->AppendReferences(references);
  return Status::OK;
}

ChunkObject::ChunkObject(std::unique_ptr<const Piece> piece) : piece_(std::move(piece)) {
  FXL_DCHECK(GetObjectDigestInfo(piece_->GetIdentifier().object_digest()).is_chunk())
      << "INDEX piece " << piece_->GetIdentifier() << " cannot be used as an object.";
}

std::unique_ptr<const Piece> ChunkObject::ReleasePiece() { return std::move(piece_); }

ObjectIdentifier ChunkObject::GetIdentifier() const { return piece_->GetIdentifier(); }

Status ChunkObject::GetData(absl::string_view* data) const {
  *data = piece_->GetData();
  return Status::OK;
}

VmoObject::VmoObject(ObjectIdentifier identifier, ledger::SizedVmo vmo)
    : identifier_(std::move(identifier)), vmo_(std::move(vmo)) {}

VmoObject::~VmoObject() {
  if (vmar_) {
    vmar_.destroy();
  }
}

ObjectIdentifier VmoObject::GetIdentifier() const { return identifier_; }

Status VmoObject::GetData(absl::string_view* data) const {
  RETURN_ON_ERROR(Initialize());
  *data = data_;
  return Status::OK;
}

Status VmoObject::GetVmo(ledger::SizedVmo* vmo) const {
  zx_status_t zx_status = vmo_.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, vmo);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to duplicate a vmo: " << zx_status_get_string(zx_status);
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
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, &vmar_, &allocate_address);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate VMAR: " << zx_status_get_string(zx_status);
    return Status::INTERNAL_ERROR;
  }

  char* mapped_address;
  zx_status =
      vmar_.map(0, vmo_.vmo(), 0, vmo_.size(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                reinterpret_cast<uintptr_t*>(&mapped_address));
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map VMO: " << zx_status_get_string(zx_status);
    vmar_.reset();
    return Status::INTERNAL_ERROR;
  }

  data_ = absl::string_view(mapped_address, vmo_.size());
  initialized_ = true;

  return Status::OK;
}

}  // namespace storage
