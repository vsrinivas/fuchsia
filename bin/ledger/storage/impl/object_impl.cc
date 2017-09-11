// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/object_impl.h"

#include <utility>

namespace storage {

namespace {
uint64_t ToFullPages(uint64_t value) {
  return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
}
}  // namespace

InlinedObject::InlinedObject(ObjectId id) : id_(std::move(id)) {}
InlinedObject::~InlinedObject() {}

ObjectId InlinedObject::GetId() const {
  return id_;
}

Status InlinedObject::GetData(fxl::StringView* data) const {
  *data = id_;
  return Status::OK;
}

StringObject::StringObject(ObjectId id, std::string content)
    : id_(std::move(id)), content_(std::move(content)) {}

StringObject::~StringObject() {}

ObjectId StringObject::GetId() const {
  return id_;
}

Status StringObject::GetData(fxl::StringView* data) const {
  *data = content_;
  return Status::OK;
}

LevelDBObject::LevelDBObject(ObjectId id,
                             std::unique_ptr<leveldb::Iterator> iterator)
    : id_(std::move(id)), iterator_(std::move(iterator)) {}

LevelDBObject::~LevelDBObject() {}

ObjectId LevelDBObject::GetId() const {
  return id_;
}

Status LevelDBObject::GetData(fxl::StringView* data) const {
  *data = convert::ExtendedStringView(iterator_->value());
  return Status::OK;
}

VmoObject::VmoObject(ObjectId id, mx::vmo vmo)
    : id_(std::move(id)), vmo_(std::move(vmo)) {}

VmoObject::~VmoObject() {
  if (vmar_) {
    vmar_.destroy();
  }
}

ObjectId VmoObject::GetId() const {
  return id_;
}

Status VmoObject::GetData(fxl::StringView* data) const {
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }
  *data = data_;
  return Status::OK;
}

Status VmoObject::GetVmo(mx::vmo* vmo) const {
  Status status = Initialize();
  if (status != Status::OK) {
    return status;
  }

  mx_status_t mx_status = vmo_.duplicate(
      MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_MAP | MX_RIGHT_TRANSFER,
      vmo);
  if (mx_status != MX_OK) {
    return Status::INTERNAL_IO_ERROR;
  }
  return Status::OK;
}

Status VmoObject::Initialize() const {
  if (initialized_) {
    return Status::OK;
  }

  size_t size;
  mx_status_t mx_status = vmo_.get_size(&size);
  if (mx_status != MX_OK) {
    FXL_LOG(ERROR) << "Unable to get VMO size. Error: " << mx_status;
    return Status::INTERNAL_IO_ERROR;
  }

  uintptr_t allocate_address;
  mx_status = mx::vmar::root_self().allocate(0, ToFullPages(size),
                                             MX_VM_FLAG_CAN_MAP_READ |
                                                 MX_VM_FLAG_CAN_MAP_WRITE |
                                                 MX_VM_FLAG_CAN_MAP_SPECIFIC,
                                             &vmar_, &allocate_address);
  if (mx_status != MX_OK) {
    FXL_LOG(ERROR) << "Unable to allocate VMAR. Error: " << mx_status;
    return Status::INTERNAL_IO_ERROR;
  }

  char* mapped_address;
  mx_status = vmar_.map(
      0, vmo_, 0, size,
      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
      reinterpret_cast<uintptr_t*>(&mapped_address));
  if (mx_status != MX_OK) {
    FXL_LOG(ERROR) << "Unable to map VMO. Error: " << mx_status;
    vmar_.reset();
    return Status::INTERNAL_IO_ERROR;
  }

  data_ = fxl::StringView(mapped_address, size);
  initialized_ = true;

  return Status::OK;
}

}  // namespace storage
