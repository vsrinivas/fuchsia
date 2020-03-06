// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer.h"

#include <lib/syslog/global.h>
#include <zircon/status.h>

namespace network {
namespace tun {

zx_status_t VmoStore::GetMappedVmo(uint8_t id, fbl::Span<uint8_t>* out_span) {
  auto& stored_vmo = vmos_[id];
  if (!stored_vmo.has_value()) {
    return ZX_ERR_NOT_FOUND;
  }
  *out_span = fbl::Span<uint8_t>(static_cast<uint8_t*>(stored_vmo->mapper.start()),
                                 stored_vmo->mapper.size());
  return ZX_OK;
}

zx_status_t VmoStore::RegisterVmo(uint8_t id, zx::vmo vmo) {
  if (id >= MAX_VMOS) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmos_[id].has_value()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  StoredVmo stored_vmo{std::move(vmo), fzl::VmoMapper()};
  zx_status_t status =
      stored_vmo.mapper.Map(stored_vmo.vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    return status;
  }
  vmos_[id] = std::move(stored_vmo);
  return ZX_OK;
}

zx_status_t VmoStore::UnregisterVmo(uint8_t id) {
  if (id >= MAX_VMOS) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto& stored_vmo = vmos_[id];
  if (!stored_vmo.has_value()) {
    return ZX_ERR_NOT_FOUND;
  }
  stored_vmo.reset();
  return ZX_OK;
}

zx_status_t VmoStore::Copy(VmoStore* src_store, uint8_t src_id, size_t src_offset,
                           VmoStore* dst_store, uint8_t dst_id, size_t dst_offset, size_t len) {
  fbl::Span<uint8_t> src, dst;
  zx_status_t status = src_store->GetMappedVmo(src_id, &src);
  if (status != ZX_OK) {
    return status;
  }
  status = dst_store->GetMappedVmo(dst_id, &dst);
  if (status != ZX_OK) {
    return status;
  }
  if (src_offset + len > src.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (dst_offset + len > dst.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  std::copy_n(src.begin() + src_offset, len, dst.begin() + dst_offset);
  return ZX_OK;
}

Buffer VmoStore::MakeTxBuffer(const tx_buffer_t* tx, bool get_meta) {
  return Buffer(tx, get_meta, this);
}

Buffer VmoStore::MakeRxSpaceBuffer(const rx_space_buffer_t* space) { return Buffer(space, this); }

Buffer::Buffer(const tx_buffer_t* tx, bool get_meta, VmoStore* vmo_store)
    : id_(tx->id),
      vmo_store_(vmo_store),
      vmo_id_(tx->virtual_mem.vmo_id),
      parts_count_(tx->virtual_mem.parts_count),
      frame_type_(static_cast<fuchsia::hardware::network::FrameType>(tx->meta.frame_type)) {
  // Enforce the banjo contract.
  ZX_ASSERT(tx->virtual_mem.parts_count <= MAX_VIRTUAL_PARTS);
  std::copy_n(tx->virtual_mem.parts_list, parts_count_, parts_.begin());
  if (get_meta) {
    meta_ = fuchsia::net::tun::FrameMetadata::New();
    meta_->flags = tx->meta.flags;
    meta_->info_type = static_cast<fuchsia::hardware::network::InfoType>(tx->meta.info_type);
    if (meta_->info_type != fuchsia::hardware::network::InfoType::NO_INFO) {
      FX_LOGF(WARNING, "tun", "Unrecognized InfoType %d", tx->meta.info_type);
    }
  }
}

Buffer::Buffer(const rx_space_buffer_t* space, VmoStore* vmo_store)
    : id_(space->id),
      vmo_store_(vmo_store),
      vmo_id_(space->virtual_mem.vmo_id),
      parts_count_(space->virtual_mem.parts_count) {
  // Enforce the banjo contract.
  ZX_ASSERT(space->virtual_mem.parts_count <= MAX_VIRTUAL_PARTS);
  std::copy_n(space->virtual_mem.parts_list, parts_count_, parts_.begin());
}

zx_status_t Buffer::Read(std::vector<uint8_t>* vec) {
  size_t total = 0;
  auto inserter = std::back_inserter(*vec);
  for (size_t i = 0; i < parts_count_; i++) {
    auto len = parts_[i].length;
    zx_status_t status = vmo_store_->Read(vmo_id_, parts_[i].offset, len, inserter);
    if (status != ZX_OK) {
      return status;
    }
    total += len;
  }
  return ZX_OK;
}

zx_status_t Buffer::Write(const std::vector<uint8_t>& data) {
  auto len = data.size();
  size_t offset = 0;
  size_t idx = 0;
  while (len && idx < parts_count_) {
    auto wr = len > parts_[idx].length ? parts_[idx].length : len;
    auto status = vmo_store_->Write(vmo_id_, parts_[idx].offset, wr, &data[offset]);
    if (status != ZX_OK) {
      return status;
    }
    len -= wr;
    offset += wr;
    idx++;
  }
  if (len != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

zx_status_t Buffer::CopyFrom(Buffer* other, size_t* total) {
  size_t copied = 0;
  size_t idx_me = 0;
  uint64_t offset_me = 0;
  uint64_t offset_other = 0;
  for (size_t idx_o = 0; idx_o < other->parts_count_;) {
    if (idx_me >= parts_count_) {
      FX_LOG(ERROR, "tun", "Buffer: not enough space on rx buffer");
      return ZX_ERR_INTERNAL;
    }
    auto len_o = other->parts_[idx_o].length - offset_other;
    auto len_me = parts_[idx_me].length - offset_me;
    auto wr = len_o > len_me ? len_me : len_o;

    zx_status_t status = VmoStore::Copy(other->vmo_store_, other->vmo_id_,
                                        other->parts_[idx_o].offset + offset_other, vmo_store_,
                                        vmo_id_, parts_[idx_me].offset + offset_me, wr);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "Buffer: failed to copy between buffers: %s",
              zx_status_get_string(status));
      return status;
    }

    offset_me += wr;
    offset_other += wr;
    copied += wr;
    if (offset_me >= parts_[idx_me].length) {
      idx_me++;
      offset_me = 0;
    }
    if (offset_other >= other->parts_[idx_o].length) {
      idx_o++;
      offset_other = 0;
    }
  }
  *total = copied;
  return ZX_OK;
}

}  // namespace tun
}  // namespace network
