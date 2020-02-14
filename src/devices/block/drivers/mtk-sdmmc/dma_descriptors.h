// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_MTK_SDMMC_DMA_DESCRIPTORS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_MTK_SDMMC_DMA_DESCRIPTORS_H_

#include "mtk-sdmmc.h"

namespace {

static constexpr size_t kDescriptorChecksumSize = 16;
static constexpr uint64_t kAddressMask = 0xffffffff;

uint32_t CalculateChecksum(const void* buf) {
  const uint8_t* buf_u8 = reinterpret_cast<const uint8_t*>(buf);
  uint32_t sum = 0;
  for (size_t i = 0; i < kDescriptorChecksumSize; i++) {
    sum += buf_u8[i];
  }

  return (0xff - (sum & 0xff)) & 0xff;
}

}  // namespace

namespace sdmmc {

struct GpDmaDescriptor {
  void SetNext(uint64_t addr) {
    info = GpDmaDescriptorInfo().set_reg_value(info).set_next_addr(addr).reg_value();
    next = addr & kAddressMask;
  }

  void SetBDmaDesc(uint64_t addr) {
    info = GpDmaDescriptorInfo().set_reg_value(info).set_bdma_desc_addr(addr).reg_value();
    bdma_desc = addr & kAddressMask;
  }

  void SetChecksum() {
    auto info_reg = GpDmaDescriptorInfo().set_reg_value(info).set_checksum(0);
    info = info_reg.reg_value();
    info = info_reg.set_checksum(CalculateChecksum(this)).reg_value();
  }

  uint32_t info = 0;       // See GpDmaDescriptorInfo in mtk-sdmmc-reg.h.
  uint32_t next = 0;       // Physical address of the next GpDmaDesecriptor.
  uint32_t bdma_desc = 0;  // Physical address of the BDmaDescriptor.
  uint32_t size = 0;       // Values here and below are ignored when using one GPDMA descriptor at
  uint32_t arg = 0;        // a time.
  uint32_t blknum = 0;
  uint32_t cmd = 0;
};

struct BDmaDescriptor {
  static constexpr size_t kMaxBufferSize = 0xffff & ~kPageMask;
  static_assert(kMaxBufferSize > 0);
  static_assert(kMaxBufferSize % PAGE_SIZE == 0);

  void SetNext(uint64_t addr) {
    info = BDmaDescriptorInfo().set_reg_value(info).set_next_addr(addr).reg_value();
    next = addr & kAddressMask;
  }

  void SetBuffer(uint64_t addr) {
    info = BDmaDescriptorInfo().set_reg_value(info).set_buffer_addr(addr).reg_value();
    buffer = addr & kAddressMask;
  }

  void SetChecksum() {
    auto info_reg = BDmaDescriptorInfo().set_reg_value(info).set_checksum(0);
    info = info_reg.reg_value();
    info = info_reg.set_checksum(CalculateChecksum(this)).reg_value();
  }

  uint32_t info = 0;    // See BDmaDescriptorInfo in mtk-sdmmc-reg.h.
  uint32_t next = 0;    // Physical address of the next BDmaDescriptor.
  uint32_t buffer = 0;  // Physical address of the data buffer.
  uint32_t size = 0;    // Size of the data buffer.
};

}  // namespace sdmmc

#endif  // SRC_STORAGE_BLOCK_DRIVERS_MTK_SDMMC_DMA_DESCRIPTORS_H_
