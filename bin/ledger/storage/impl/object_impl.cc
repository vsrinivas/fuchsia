// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_impl.h"

#include <utility>

namespace storage {

namespace {
uint64_t ToFullPages(uint64_t value) {
  return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
}
}  // namespace

InlinedObject::InlinedObject(ObjectIdentifier identifier)
    : identifier_(std::move(identifier)) {}
InlinedObject::~InlinedObject() {}

ObjectIdentifier InlinedObject::GetIdentifier() const { return identifier_; }

Status InlinedObject::GetData(fxl::StringView* data) const {
  *data = identifier_.object_digest;
  return Status::OK;
}

StringObject::StringObject(ObjectIdentifier identifier, std::string content)
    : identifier_(std::move(identifier)), content_(std::move(content)) {}

StringObject::~StringObject() {}

ObjectIdentifier StringObject::GetIdentifier() const { return identifier_; }

Status StringObject::GetData(fxl::StringView* data) const {
  *data = content_;
  return Status::OK;
}

LevelDBObject::LevelDBObject(ObjectIdentifier identifier,
                             std::unique_ptr<leveldb::Iterator> iterator)
    : identifier_(std::move(identifier)), iterator_(std::move(iterator)) {}

LevelDBObject::~LevelDBObject() {}

ObjectIdentifier LevelDBObject::GetIdentifier() const { return identifier_; }

Status LevelDBObject::GetData(fxl::StringView* data) const {
  *data = convert::ExtendedStringView(iterator_->value());
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
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }

  zx_status_t zx_status =
      vmo_.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, vmo);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to duplicate a vmo. Status: " << zx_status;
    return Status::INTERNAL_IO_ERROR;
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
      ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE |
          ZX_VM_FLAG_CAN_MAP_SPECIFIC,
      &vmar_, &allocate_address);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate VMAR. Error: " << zx_status;
    return Status::INTERNAL_IO_ERROR;
  }

  char* mapped_address;
  zx_status = vmar_.map(
      0, vmo_.vmo(), 0, vmo_.size(),
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC,
      reinterpret_cast<uintptr_t*>(&mapped_address));
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map VMO. Error: " << zx_status;
    vmar_.reset();
    return Status::INTERNAL_IO_ERROR;
  }

  data_ = fxl::StringView(mapped_address, vmo_.size());
  initialized_ = true;

  return Status::OK;
}

}  // namespace storage
