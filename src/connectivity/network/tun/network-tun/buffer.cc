// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer.h"

#include <lib/syslog/global.h>
#include <zircon/status.h>

namespace network {
namespace tun {

zx::status<fbl::Span<uint8_t>> VmoStore::GetMappedVmo(uint8_t id) {
  auto* stored_vmo = store_.GetVmo(id);
  if (!stored_vmo) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(stored_vmo->data());
}

zx_status_t VmoStore::RegisterVmo(uint8_t id, zx::vmo vmo) {
  // Lazily reserve storage space.
  // Reserve will be a no-op if we already have `MAX_VMOS` capacity.
  zx_status_t status = store_.Reserve(MAX_VMOS);
  if (status != ZX_OK) {
    return status;
  }
  return store_.RegisterWithKey(id, std::move(vmo));
}

zx_status_t VmoStore::UnregisterVmo(uint8_t id) { return store_.Unregister(id).status_value(); }

zx_status_t VmoStore::Copy(VmoStore* src_store, uint8_t src_id, size_t src_offset,
                           VmoStore* dst_store, uint8_t dst_id, size_t dst_offset, size_t len) {
  zx::status src = src_store->GetMappedVmo(src_id);
  if (src.is_error()) {
    return src.error_value();
  }
  zx::status dst = dst_store->GetMappedVmo(dst_id);
  if (dst.is_error()) {
    return dst.error_value();
  }
  if (src_offset + len > src->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (dst_offset + len > dst->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  std::copy_n(src->begin() + src_offset, len, dst->begin() + dst_offset);
  return ZX_OK;
}

Buffer VmoStore::MakeTxBuffer(const tx_buffer_t* tx, bool get_meta) {
  return Buffer(tx, get_meta, this);
}

Buffer VmoStore::MakeRxSpaceBuffer(const rx_space_buffer_t* space) { return Buffer(space, this); }

Buffer::Buffer(const tx_buffer_t* tx, bool get_meta, VmoStore* vmo_store)
    : id_(tx->id),
      vmo_store_(vmo_store),
      vmo_id_(tx->data.vmo_id),
      parts_count_(tx->data.parts_count),
      frame_type_(static_cast<fuchsia_hardware_network::wire::FrameType>(tx->meta.frame_type)) {
  // Enforce the banjo contract.
  ZX_ASSERT(tx->data.parts_count <= MAX_BUFFER_PARTS);
  std::copy_n(tx->data.parts_list, parts_count_, parts_.begin());
  if (get_meta) {
    meta_ = {
        .info_type = static_cast<fuchsia_hardware_network::wire::InfoType>(tx->meta.info_type),
        .flags = tx->meta.flags,
    };
    if (meta_->info_type != fuchsia_hardware_network::wire::InfoType::kNoInfo) {
      FX_LOGF(WARNING, "tun", "Unrecognized InfoType %d", tx->meta.info_type);
    }
  }
}

Buffer::Buffer(const rx_space_buffer_t* space, VmoStore* vmo_store)
    : id_(space->id),
      vmo_store_(vmo_store),
      vmo_id_(space->data.vmo_id),
      parts_count_(space->data.parts_count) {
  // Enforce the banjo contract.
  ZX_ASSERT(space->data.parts_count <= MAX_BUFFER_PARTS);
  std::copy_n(space->data.parts_list, parts_count_, parts_.begin());
}

zx_status_t Buffer::Read(std::vector<uint8_t>& vec) {
  size_t total = 0;
  auto inserter = std::back_inserter(vec);
  for (const buffer_region_t& part : fbl::Span(parts_.data(), parts_count_)) {
    size_t len = part.length;
    zx_status_t status = vmo_store_->Read(vmo_id_, part.offset, len, inserter);
    if (status != ZX_OK) {
      return status;
    }
    total += len;
  }
  return ZX_OK;
}

zx_status_t Buffer::Write(const uint8_t* data, size_t count) {
  for (const buffer_region_t& part : fbl::Span(parts_.data(), parts_count_)) {
    if (count == 0) {
      break;
    }
    size_t len = std::min(count, part.length);
    zx_status_t status = vmo_store_->Write(vmo_id_, part.offset, len, data);
    if (status != ZX_OK) {
      return status;
    }
    data += len;
    count -= len;
  }
  if (count == 0) {
    return ZX_OK;
  }
  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t Buffer::Write(const fidl::VectorView<uint8_t>& data) {
  return Write(data.data(), data.count());
}

zx_status_t Buffer::Write(const std::vector<uint8_t>& data) {
  return Write(data.data(), data.size());
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
