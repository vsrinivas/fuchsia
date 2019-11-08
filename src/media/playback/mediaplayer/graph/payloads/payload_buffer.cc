// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/payloads/payload_buffer.h"

#include <lib/zx/vmar.h>

#include <cstdlib>
#include <memory>

namespace media_player {

// static
fbl::RefPtr<PayloadVmo> PayloadVmo::Create(uint64_t vmo_size, zx_vm_option_t map_flags) {
  FX_DCHECK(vmo_size != 0);

  zx::vmo vmo;

  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create VMO of size " << vmo_size;
    return nullptr;
  }

  auto result = fbl::MakeRefCounted<PayloadVmo>(std::move(vmo), vmo_size, map_flags, &status);
  return status == ZX_OK ? result : nullptr;
}

// static
fbl::RefPtr<PayloadVmo> PayloadVmo::Create(zx::vmo vmo, zx_vm_option_t map_flags) {
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get VMO size";
    return nullptr;
  }

  auto result = fbl::MakeRefCounted<PayloadVmo>(std::move(vmo), vmo_size, map_flags, &status);
  return status == ZX_OK ? result : nullptr;
}

PayloadVmo::PayloadVmo(zx::vmo vmo, uint64_t vmo_size, zx_vm_option_t map_flags,
                       zx_status_t* status_out)
    : vmo_(std::move(vmo)), size_(vmo_size) {
  FX_DCHECK(vmo_);
  FX_DCHECK(vmo_size != 0);
  FX_DCHECK(status_out != nullptr);

  if (map_flags != 0) {
    zx_status_t status = vmo_mapper_.Map(vmo_, 0, size_, map_flags, nullptr);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to map VMO, size " << size_ << ", map_flags " << map_flags;
      *status_out = status;
      return;
    }
  }

  *status_out = ZX_OK;
}

zx::vmo PayloadVmo::Duplicate(zx_rights_t rights) {
  zx::vmo duplicate;
  zx_status_t status = vmo_.duplicate(rights, &duplicate);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to duplicate VMO, rights " << rights;
  }

  return duplicate;
}

// static
fbl::RefPtr<PayloadBuffer> PayloadBuffer::Create(uint64_t size, void* data, Recycler recycler) {
  return fbl::AdoptRef(new PayloadBuffer(size, data, std::move(recycler)));
}

// static
fbl::RefPtr<PayloadBuffer> PayloadBuffer::Create(uint64_t size, void* data,
                                                 fbl::RefPtr<PayloadVmo> vmo, uint64_t offset,
                                                 Recycler recycler) {
  return fbl::AdoptRef(new PayloadBuffer(size, data, vmo, offset, std::move(recycler)));
}

// static
fbl::RefPtr<PayloadBuffer> PayloadBuffer::CreateWithMalloc(uint64_t size) {
  FX_DCHECK(size > 0);
  return PayloadBuffer::Create(
      size, std::aligned_alloc(PayloadBuffer::kByteAlignment, PayloadBuffer::AlignUp(size)),
      [](PayloadBuffer* payload_buffer) {
        FX_DCHECK(payload_buffer);
        std::free(payload_buffer->data());
        // The |PayloadBuffer| deletes itself.
      });
}

PayloadBuffer::PayloadBuffer(uint64_t size, void* data, Recycler recycler)
    : size_(size), data_(data), recycler_(std::move(recycler)) {
  FX_DCHECK(size_ != 0);
  FX_DCHECK(data_ != nullptr);
  FX_DCHECK(recycler_);
}

PayloadBuffer::PayloadBuffer(uint64_t size, void* data, fbl::RefPtr<PayloadVmo> vmo,
                             uint64_t offset_in_vmo, Recycler recycler)
    : size_(size), data_(data), vmo_(vmo), offset_(offset_in_vmo), recycler_(std::move(recycler)) {
  FX_DCHECK(size_ != 0);
  FX_DCHECK(vmo_);
  FX_DCHECK((data_ == nullptr) || (reinterpret_cast<uint8_t*>(vmo_->start()) + offset_ ==
                                    reinterpret_cast<uint8_t*>(data_)));
  FX_DCHECK(recycler_);
}

PayloadBuffer::~PayloadBuffer() {
  FX_DCHECK(!recycler_) << "PayloadBuffers must delete themselves.";
}

void PayloadBuffer::AfterRecycling(Action action) {
  FX_DCHECK(!after_recycling_) << "AfterRecycling may only be called once.";
  after_recycling_ = std::move(action);
}

void PayloadBuffer::fbl_recycle() {
  FX_DCHECK(recycler_ != nullptr);

  recycler_(this);
  // This tells the destructor that deletion is being done properly.
  recycler_ = nullptr;

  if (after_recycling_) {
    after_recycling_(this);
  }

  delete this;
}

}  // namespace media_player
