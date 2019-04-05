// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>

#include <lib/mmio/mmio.h>

#include <fbl/algorithm.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::SocInit() {
    zx_status_t status;
    mmio_buffer_t mmio;
    status = mmio_buffer_init_physical(&mmio, MT8167_SOC_BASE, MT8167_SOC_SIZE,
                                       get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: mmio_buffer_init_physical failed %d \n", __FUNCTION__, status);
        return status;
    }
    ddk::MmioBuffer mmio2(mmio);

    // 1 to invert from Low to High, 0 is either already High or a reserved interrupt
    static constexpr bool L = 1;
    static constexpr bool H = 0;
    static constexpr bool R = 0;

    static const bool spi_polarities[] = {
        L, L, L, L, R, R, R, R, L, L, L, L, R, R, R, R, // 32 (first interrupt in the line).
        L, L, L, L, R, R, R, R, L, L, L, L, R, R, R, R, // 48.
        L, L, L, L, R, R, R, R, L, L, L, L, R, R, R, R, // 64.
        L, R, L, L, L, L, R, R, R, R, R, R, R, R, R, L, // 80.
        H, H, H, H, H, H, H, H, L, L, R, L, L, L, L, L, // 96.
        L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, // 112.
        L, L, L, L, L, L, L, L, L, H, H, L, H, L, L, L, // 128.
        L, L, L, L, H, L, L, L, L, L, L, L, L, L, L, L, // 144.
        L, L, L, L, L, H, H, L, L, L, L, L, L, L, L, L, // 160.
        L, L, L, L, R, L, L, L, L, L, L, L, L, L, L, L, // 176.
        L, R, L, L, L, L, L, L, L, L, R, L, L, L, L, L, // 192.
        L, L, L, L, L, L, L, L, L, L, L, L, L, L, L, R, // 208.
        R, R, L, L, L, L, L, L, L, L, L, R, L, H, H, H, // 224.
        H, L, L, L, R, R, L, H, H, H, H                 // 240 (first is 240, last is 250).
    };

    // Start from interrupt 32 (first SPI after 32 PPIs)
    // Convert Level interrupt polarity in SOC from Low to High as needed by gicv2.
    for (size_t i = 0; i < fbl::count_of(spi_polarities); ++i) {
        // 32 interrupts per register, one register every 4 bytes.
        mmio2.ModifyBit<uint32_t>(spi_polarities[i], i % 32, MT8167_SOC_INT_POL + i / 32 * 4);
    }
    return ZX_OK;
}

} // namespace board_mt8167
