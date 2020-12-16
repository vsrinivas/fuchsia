// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_BUFFER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_BUFFER_H_

#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>

namespace wlan {
namespace brcmfmac {

// This class holds a page-aligned memory buffer.  If it is created with access to a device BTI,
// then it is allocated contiguously and has a single DMA address to the start of the buffer.  The
// CPU address of the buffer can be obtained (and released) by Map() or Unmap() on the buffer.  On
// destruction, the DmaBuffer instance will automatically unmap any outstanding CPU mapping, and
// unpin any outstanding DMA mapping.
class DmaBuffer {
 public:
  DmaBuffer();
  DmaBuffer(const DmaBuffer& other) = delete;
  DmaBuffer(DmaBuffer&& other);
  DmaBuffer& operator=(DmaBuffer other);
  friend void swap(DmaBuffer& lhs, DmaBuffer& rhs);
  ~DmaBuffer();

  // Static factory function for DmaBuffer instances.  The DmaBufer of size `size` and cache policy
  // `cache_policy` is device-visible iff `bti` is also provided.
  static zx_status_t Create(const zx::bti* bti, uint32_t cache_policy, size_t size,
                            std::unique_ptr<DmaBuffer>* out_dma_buffer);

  // Map and unmap the DmaBuffer for CPU access.  The address of the mapping can be retrieved with
  // address(), and it will be unmapped on destruction.
  zx_status_t Map(uint32_t vmar_options);
  zx_status_t Unmap();

  // Map the DmaBuffer on a provided VMAR.  This mapping will not be automatically unmapped on
  // destruction.
  zx_status_t Map(const zx::vmar& vmar, uint32_t vmar_options, uintptr_t* out_address);

  // State accessors.
  bool is_valid() const;
  size_t size() const;
  uint32_t cache_policy() const;
  zx_paddr_t dma_address() const;
  uintptr_t address() const;
  const zx::vmo& vmo() const;

 protected:
  zx::vmo vmo_;
  zx::pmt pmt_;
  size_t size_ = 0;
  uint32_t cache_policy_ = 0;
  zx_paddr_t dma_address_ = 0;
  uintptr_t address_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DMA_BUFFER_H_
