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

InlinedObject::InlinedObject(ObjectDigest digest)
    : digest_(std::move(digest)) {}
InlinedObject::~InlinedObject() {}

ObjectDigest InlinedObject::GetDigest() const {
  return digest_;
}

Status InlinedObject::GetData(fxl::StringView* data) const {
  *data = digest_;
  return Status::OK;
}

StringObject::StringObject(ObjectDigest digest, std::string content)
    : digest_(std::move(digest)), content_(std::move(content)) {}

StringObject::~StringObject() {}

ObjectDigest StringObject::GetDigest() const {
  return digest_;
}

Status StringObject::GetData(fxl::StringView* data) const {
  *data = content_;
  return Status::OK;
}

LevelDBObject::LevelDBObject(ObjectDigest digest,
                             std::unique_ptr<leveldb::Iterator> iterator)
    : digest_(std::move(digest)), iterator_(std::move(iterator)) {}

LevelDBObject::~LevelDBObject() {}

ObjectDigest LevelDBObject::GetDigest() const {
  return digest_;
}

Status LevelDBObject::GetData(fxl::StringView* data) const {
  *data = convert::ExtendedStringView(iterator_->value());
  return Status::OK;
}

VmoObject::VmoObject(ObjectDigest digest, zx::vmo vmo)
    : digest_(std::move(digest)), vmo_(std::move(vmo)) {}

VmoObject::~VmoObject() {
  if (vmar_) {
    vmar_.destroy();
  }
}

ObjectDigest VmoObject::GetDigest() const {
  return digest_;
}

Status VmoObject::GetData(fxl::StringView* data) const {
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }
  *data = data_;
  return Status::OK;
}

Status VmoObject::GetVmo(zx::vmo* vmo) const {
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }

  zx_status_t zx_status = vmo_.duplicate(
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER,
      vmo);
  if (zx_status != ZX_OK) {
    return Status::INTERNAL_IO_ERROR;
  }
  return Status::OK;
}

Status VmoObject::Initialize() const {
  if (initialized_) {
    return Status::OK;
  }

  size_t size;
  zx_status_t zx_status = vmo_.get_size(&size);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to get VMO size. Error: " << zx_status;
    return Status::INTERNAL_IO_ERROR;
  }

  uintptr_t allocate_address;
  zx_status = zx::vmar::root_self().allocate(0, ToFullPages(size),
                                             ZX_VM_FLAG_CAN_MAP_READ |
                                                 ZX_VM_FLAG_CAN_MAP_WRITE |
                                                 ZX_VM_FLAG_CAN_MAP_SPECIFIC,
                                             &vmar_, &allocate_address);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate VMAR. Error: " << zx_status;
    return Status::INTERNAL_IO_ERROR;
  }

  char* mapped_address;
  zx_status = vmar_.map(
      0, vmo_, 0, size,
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC,
      reinterpret_cast<uintptr_t*>(&mapped_address));
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map VMO. Error: " << zx_status;
    vmar_.reset();
    return Status::INTERNAL_IO_ERROR;
  }

  data_ = fxl::StringView(mapped_address, size);
  initialized_ = true;

  return Status::OK;
}

}  // namespace storage
