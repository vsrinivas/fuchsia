// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer.h"

#include <lib/syslog/global.h>
#include <zircon/status.h>

namespace network {
namespace tun {

zx::result<cpp20::span<uint8_t>> VmoStore::GetMappedVmo(uint8_t id) {
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

zx_status_t VmoStore::Copy(VmoStore& src_store, uint8_t src_id, size_t src_offset,
                           VmoStore& dst_store, uint8_t dst_id, size_t dst_offset, size_t len) {
  zx::result src = src_store.GetMappedVmo(src_id);
  if (src.is_error()) {
    return src.error_value();
  }
  zx::result dst = dst_store.GetMappedVmo(dst_id);
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

TxBuffer VmoStore::MakeTxBuffer(const tx_buffer_t& tx, bool get_meta) {
  return TxBuffer(tx, get_meta, this);
}

RxBuffer VmoStore::MakeRxSpaceBuffer(const rx_space_buffer_t& space) {
  RxBuffer b(this);
  b.PushRxSpace(space);
  return b;
}

RxBuffer VmoStore::MakeEmptyRxBuffer() { return RxBuffer(this); }

void Buffer::PushPart(const BufferPart& part) {
  ZX_DEBUG_ASSERT(parts_count_ < parts_.size());
  parts_[parts_count_++] = part;
  total_length_ += part.region.length;
}

zx_status_t Buffer::Read(std::vector<uint8_t>& vec) {
  auto inserter = std::back_inserter(vec);
  for (const BufferPart& part : parts()) {
    zx_status_t status =
        vmo_store_->Read(part.region.vmo, part.region.offset, part.region.length, inserter);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Buffer::Write(const uint8_t* data, size_t count) {
  for (const BufferPart& part : parts()) {
    if (count == 0) {
      break;
    }
    size_t len = std::min(count, part.region.length);
    zx_status_t status = vmo_store_->Write(part.region.vmo, part.region.offset, len, data);
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

zx::result<size_t> Buffer::CopyFrom(Buffer& other) {
  size_t copied = 0;
  uint64_t offset_me = 0;
  uint64_t offset_other = 0;

  cpp20::span parts_other = other.parts();
  cpp20::span parts_me = parts();

  auto part_me = parts_me.begin();
  for (auto part_o = parts_other.begin(); part_o != parts_other.end();) {
    if (part_me == parts_me.end()) {
      FX_LOG(ERROR, "tun", "Buffer: not enough space on rx buffer");
      return zx::error(ZX_ERR_INTERNAL);
    }

    uint64_t len_o = part_o->region.length - offset_other;
    uint64_t len_me = part_me->region.length - offset_me;
    uint64_t wr = len_o > len_me ? len_me : len_o;

    zx_status_t status =
        VmoStore::Copy(*other.vmo_store_, part_o->region.vmo, part_o->region.offset + offset_other,
                       *vmo_store_, part_me->region.vmo, part_me->region.offset + offset_me, wr);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "Buffer: failed to copy between buffers: %s",
              zx_status_get_string(status));
      return zx::error(status);
    }

    offset_me += wr;
    offset_other += wr;
    copied += wr;
    if (offset_me >= part_me->region.length) {
      part_me++;
      offset_me = 0;
    }
    if (offset_other >= part_o->region.length) {
      part_o++;
      offset_other = 0;
    }
  }
  return zx::ok(copied);
}

TxBuffer::TxBuffer(const tx_buffer_t& tx, bool get_meta, VmoStore* vmo_store)
    : Buffer(vmo_store),
      port_id_(tx.meta.port),
      frame_type_(static_cast<fuchsia_hardware_network::wire::FrameType>(tx.meta.frame_type)) {
  // Enforce the banjo contract.
  ZX_ASSERT(tx.data_count <= MAX_BUFFER_PARTS);
  for (const buffer_region_t& region : cpp20::span(tx.data_list, tx.data_count)) {
    PushPart(BufferPart{
        .buffer_id = tx.id,
        .region = region,
    });
  }
  if (get_meta) {
    auto info_type = static_cast<fuchsia_hardware_network::wire::InfoType>(tx.meta.info_type);
    if (info_type != fuchsia_hardware_network::wire::InfoType::kNoInfo) {
      FX_LOGF(WARNING, "tun", "Unrecognized InfoType %d", tx.meta.info_type);
    }
    meta_ = fuchsia_net_tun::wire::FrameMetadata{
        .info_type = info_type,
        .flags = tx.meta.flags,
    };
  }
}

void RxBuffer::PushRxSpace(const rx_space_buffer_t& space) {
  PushPart(BufferPart{
      .buffer_id = space.id,
      .region = space.region,
  });
}

}  // namespace tun
}  // namespace network
