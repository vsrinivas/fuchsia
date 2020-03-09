// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-spi.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>

#include "registers.h"

namespace spi {

zx_status_t MtkSpi::SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                                    uint8_t* out_rxdata, size_t rxdata_size,
                                    size_t* out_rxdata_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkSpi::Create(void* ctx, zx_device_t* device) { return ZX_OK; }

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = MtkSpi::Create;
  return ops;
}();

}  // namespace spi

// clang-format off
ZIRCON_DRIVER_BEGIN(mtk_spi, spi::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_SPI),
ZIRCON_DRIVER_END(mtk_spi)
