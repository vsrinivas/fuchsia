// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/lib/transport/shared_buffer_set.h"

#include <mx/vmo.h>

#include "lib/ftl/logging.h"

namespace media {

SharedBufferSet::SharedBufferSet(uint32_t local_map_flags)
    : local_map_flags_(local_map_flags) {}

SharedBufferSet::~SharedBufferSet() {}

mx_status_t SharedBufferSet::AddBuffer(uint32_t buffer_id, mx::vmo vmo) {
  if (buffer_id >= buffers_.size()) {
    buffers_.resize(buffer_id + 1);
  } else {
    FTL_DCHECK(!buffers_[buffer_id]);
  }

  MappedSharedBuffer* mapped_shared_buffer = new MappedSharedBuffer();
  mx_status_t status =
      mapped_shared_buffer->InitFromVmo(std::move(vmo), local_map_flags_);

  if (status == NO_ERROR) {
    AddBuffer(buffer_id, mapped_shared_buffer);
  }

  return status;
}

mx_status_t SharedBufferSet::CreateNewBuffer(uint64_t size,
                                             uint32_t* buffer_id_out,
                                             mx_rights_t vmo_rights,
                                             mx::vmo* out_vmo) {
  FTL_DCHECK(size != 0);
  FTL_DCHECK(buffer_id_out != nullptr);
  FTL_DCHECK(out_vmo != nullptr);

  uint32_t buffer_id = AllocateBufferId();

  MappedSharedBuffer* mapped_shared_buffer = new MappedSharedBuffer();
  mx_status_t status = mapped_shared_buffer->InitNew(size, local_map_flags_);

  if (status == NO_ERROR) {
    *buffer_id_out = buffer_id;
    *out_vmo = mapped_shared_buffer->GetDuplicateVmo(vmo_rights);
    AddBuffer(buffer_id, mapped_shared_buffer);
  }

  return status;
}

void SharedBufferSet::RemoveBuffer(uint32_t buffer_id) {
  FTL_DCHECK(buffer_id < buffers_.size());
  FTL_DCHECK(buffers_[buffer_id]);
  buffer_ids_by_base_address_.erase(
      reinterpret_cast<uint8_t*>(buffers_[buffer_id]->PtrFromOffset(0)));
  buffers_[buffer_id].reset();
}

void SharedBufferSet::Reset() {
  buffers_.clear();
  buffer_ids_by_base_address_.clear();
}

bool SharedBufferSet::Validate(const Locator& locator, uint64_t size) const {
  if (!locator || locator.buffer_id() >= buffers_.size()) {
    return false;
  }

  const std::unique_ptr<MappedSharedBuffer>& buffer =
      buffers_[locator.buffer_id()];

  if (!buffer) {
    return false;
  }

  return buffer->Validate(locator.offset(), size);
}

void* SharedBufferSet::PtrFromLocator(const Locator& locator) const {
  if (!locator) {
    return nullptr;
  }

  FTL_DCHECK(locator.buffer_id() < buffers_.size());
  const std::unique_ptr<MappedSharedBuffer>& buffer =
      buffers_[locator.buffer_id()];
  FTL_DCHECK(buffer);
  return buffer->PtrFromOffset(locator.offset());
}

SharedBufferSet::Locator SharedBufferSet::LocatorFromPtr(void* ptr) const {
  FTL_DCHECK(!buffer_ids_by_base_address_.empty());

  if (ptr == nullptr) {
    return Locator::Null();
  }

  uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(ptr);

  // upper_bound finds the first buffer whose base address is greater than the
  // supplied pointer. We want the buffer just before that. If upper_bound()
  // returns begin(), the pointer is less than any of the base addresses and
  // isn't valid.
  auto iter = buffer_ids_by_base_address_.upper_bound(byte_ptr);
  FTL_DCHECK(iter != buffer_ids_by_base_address_.begin());

  uint32_t buffer_id = (--iter)->second;
  FTL_DCHECK(buffers_[buffer_id]);

  return Locator(buffer_id, buffers_[buffer_id]->OffsetFromPtr(byte_ptr));
}

uint32_t SharedBufferSet::AllocateBufferId() {
  uint32_t buffer_id = 0;

  for (const std::unique_ptr<MappedSharedBuffer>& buffer : buffers_) {
    if (!buffer) {
      return buffer_id;
    }

    ++buffer_id;
  }

  buffers_.resize(buffer_id + 1);

  return buffer_id;
}

void SharedBufferSet::AddBuffer(uint32_t buffer_id,
                                MappedSharedBuffer* mapped_shared_buffer) {
  buffer_ids_by_base_address_.insert(std::make_pair(
      reinterpret_cast<uint8_t*>(mapped_shared_buffer->PtrFromOffset(0)),
      buffer_id));
  buffers_[buffer_id].reset(mapped_shared_buffer);
}

}  // namespace media
