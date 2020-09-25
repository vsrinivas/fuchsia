// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <array>

#include <fbl/algorithm.h>
#include <soc/mt8183/mt8183-hw.h>

#include "c18.h"

namespace board_c18 {

namespace {

constexpr size_t kNumberOfPolarityRegisters = 10;
constexpr uint32_t GetRegister(size_t offset) {
  // 1 to invert from Low to High, 0 is either already High or a reserved interrupt
  constexpr bool L = true;
  constexpr bool H = false;
  constexpr bool R = false;
  // Start from interrupt 32 (first SPI after 32 PPIs)
  constexpr std::array<bool, kNumberOfPolarityRegisters* 32> spi_polarities = {
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 32
      L, L, L, L, L, L, L, L, H, L, H, L, L, L, L, L,  // 48
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 64
      L, L, L, L, L, L, L, L, H, H, H, H, L, L, L, L,  // 80
      H, H, H, H, H, H, H, H, L, L, L, L, L, L, L, L,  // 96
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 112
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 128
      L, L, L, L, L, L, L, L, L, H, H, H, L, L, L, L,  // 144
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 160
      L, L, R, L, H, H, H, L, L, L, L, H, L, L, H, H,  // 176
      R, L, L, L, R, R, L, L, R, L, H, L, L, H, H, H,  // 192
      L, H, H, L, L, H, H, H, H, H, L, L, L, L, L, L,  // 208
      L, L, L, L, L, H, R, H, H, H, H, H, H, H, H, H,  // 224
      H, H, H, H, H, H, H, H, H, L, L, L, L, L, L, L,  // 240
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, R, L,  // 256
      L, L, L, L, L, R, R, L, L, L, L, L, L, L, L, L,  // 272
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L,  // 288
      L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, H,  // 304
      L, L, L, L, L, L, R, L, R, R, R, R, R, R, R, L   // 320
  };

  uint32_t temporary_register = 0;
  for (size_t j = 0; j < 32; ++j) {
    temporary_register |= static_cast<uint32_t>(spi_polarities[offset + j]) << j;
  }
  return temporary_register;
}

}  // namespace

zx_status_t C18::SocInit() {
  zx_status_t status;
  mmio_buffer_t mmio;
  status = mmio_buffer_init_physical(&mmio, MT8183_MCUCFG_BASE, MT8183_MCUCFG_SIZE,
                                     // Please do not use get_root_resource() in new code (fxbug.dev/31358).
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: mmio_buffer_init_physical failed %d ", __PRETTY_FUNCTION__, status);
    return status;
  }
  ddk::MmioBuffer mmio2(mmio);

  // Convert Level interrupt polarity in SOC from Low to High as needed by gicv3.
  for (size_t i = 0; i < kNumberOfPolarityRegisters; ++i) {
    // 32 interrupts per register, one register every 4 bytes.
    mmio2.Write32(GetRegister(i * 32), MT8183_MCUCFG_INT_POL_CTL0 + i * 4);
  }
  return ZX_OK;
}

}  // namespace board_c18
