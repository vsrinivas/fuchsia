// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

#include <zircon/limits.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <utility>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {

DmaBuffer::DmaBuffer() = default;

DmaBuffer::DmaBuffer(DmaBuffer&& other) { swap(*this, other); }

DmaBuffer& DmaBuffer::operator=(DmaBuffer other) {
  swap(*this, other);
  return *this;
}

void swap(DmaBuffer& lhs, DmaBuffer& rhs) {
  using std::swap;
  swap(lhs.vmo_, rhs.vmo_);
  swap(lhs.pmt_, rhs.pmt_);
  swap(lhs.size_, rhs.size_);
  swap(lhs.cache_policy_, rhs.cache_policy_);
  swap(lhs.dma_address_, rhs.dma_address_);
  swap(lhs.address_, rhs.address_);
}

DmaBuffer::~DmaBuffer() {
  if (address_ != 0) {
    Unmap();
  }
  if (pmt_.is_valid()) {
    pmt_.unpin();
  }
}

// static
zx_status_t DmaBuffer::Create(const zx::bti& bti, uint32_t cache_policy, size_t size,
                              std::unique_ptr<DmaBuffer>* out_dma_buffer) {
  zx_status_t status = ZX_OK;
  auto dma_buffer = std::make_unique<DmaBuffer>();

  // Create the VMO.
  uint32_t bti_pin_options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
  if (cache_policy == ZX_CACHE_POLICY_CACHED) {
    // For VMOs with ZX_CACHE_POLICY_CACHED, we can use zx::vmo::create_contiguous(), since
    // contiguous VMOs are by default cached.
    if ((status = zx::vmo::create_contiguous(bti, size, 0, &dma_buffer->vmo_)) != ZX_OK) {
      BRCMF_ERR("Failed to create contiguous VMO, size=%zu: %s\n", size,
                zx_status_get_string(status));
      return status;
    }
    bti_pin_options |= ZX_BTI_CONTIGUOUS;
  } else {
    // VMOs created with zx::vmo::create_contiguous() cannot have their cache policy set after
    // creation, since the creation causes pages to be committed.  So we have to use a "plain" VMO,
    // and we can only ensure that these are contiguous up to ZX_PAGE_SIZE.
    if (size > ZX_PAGE_SIZE) {
      BRCMF_ERR(
          "Failed to create uncached large VMO, size=%zu (ZX_PAGE_SIZE=%zu), cache_policy=0x%08x\n",
          size, static_cast<size_t>(ZX_PAGE_SIZE), cache_policy);
      return ZX_ERR_NO_MEMORY;
    }
    if ((status = zx::vmo::create(size, 0, &dma_buffer->vmo_)) != ZX_OK) {
      BRCMF_ERR("Failed to create VMO, size=%zu: %s\n", size, zx_status_get_string(status));
      return status;
    }
    if ((status = dma_buffer->vmo_.set_cache_policy(cache_policy)) != ZX_OK) {
      BRCMF_ERR("Failed to set cache policy, cache_policy=0x%08x: %s\n", cache_policy,
                zx_status_get_string(status));
      return status;
    }
  }

  // Double-check that we got the cache policy we wanted.
  zx_info_vmo_t vmo_info = {};
  if ((status = dma_buffer->vmo_.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr,
                                          nullptr)) != ZX_OK) {
    BRCMF_ERR("Failed to get VMO info: %s\n", zx_status_get_string(status));
    return status;
  }
  if (vmo_info.cache_policy != cache_policy) {
    BRCMF_ERR("Failed to set cache policy, expected=0x%08x actual=0x%08x\n", cache_policy,
              vmo_info.cache_policy);
    return ZX_ERR_NO_MEMORY;
  }
  dma_buffer->cache_policy_ = cache_policy;

  uint64_t actual_vmo_size = 0;
  if ((status = dma_buffer->vmo_.get_size(&actual_vmo_size)) != ZX_OK) {
    BRCMF_ERR("Failed to get VMO size, size=%zu: %s\n", size, zx_status_get_string(status));
    return status;
  }
  dma_buffer->size_ = static_cast<size_t>(actual_vmo_size);

  // Pin it.
  if ((status = bti.pin(bti_pin_options, dma_buffer->vmo_, 0, dma_buffer->size_,
                        &dma_buffer->dma_address_, 1, &dma_buffer->pmt_)) != ZX_OK) {
    BRCMF_ERR("Failed to pin VMO: %s\n", zx_status_get_string(status));
    return status;
  }

  *out_dma_buffer = std::move(dma_buffer);
  return ZX_OK;
}

zx_status_t DmaBuffer::Map(uint32_t vmar_options) {
  if (address_ != 0) {
    BRCMF_ERR("Already mapped, vmar_options=0x%08x, old=0x%08x\n", vmar_options, vmar_options);
    return ZX_ERR_BAD_STATE;
  }

  // Map it.
  return Map(*zx::vmar::root_self(), vmar_options, &address_);
}

zx_status_t DmaBuffer::Unmap() {
  zx_status_t status = ZX_OK;

  if (address_ == 0) {
    BRCMF_ERR("Not mapped\n");
    return ZX_ERR_BAD_STATE;
  }

  if ((status = zx::vmar::root_self()->unmap(address_, size_)) != ZX_OK) {
    BRCMF_ERR("Failed to unmap: %s\n", zx_status_get_string(status));
    return status;
  }

  address_ = 0;
  return ZX_OK;
}

zx_status_t DmaBuffer::Map(const zx::vmar& vmar, uint32_t vmar_options, uintptr_t* out_address) {
  zx_status_t status = ZX_OK;
  if ((status = vmar.map(0, vmo_, 0, size_, vmar_options, out_address)) != ZX_OK) {
    BRCMF_ERR("Failed to map, vmar_options=0x%08x: %s\n", vmar_options,
              zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

bool DmaBuffer::is_valid() const { return vmo_.is_valid(); }

size_t DmaBuffer::size() const { return size_; }

uint32_t DmaBuffer::cache_policy() const { return cache_policy_; }

zx_paddr_t DmaBuffer::dma_address() const { return dma_address_; }

uintptr_t DmaBuffer::address() const { return address_; }

}  // namespace brcmfmac
}  // namespace wlan
