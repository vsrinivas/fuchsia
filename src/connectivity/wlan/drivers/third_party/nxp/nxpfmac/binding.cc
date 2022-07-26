// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#if CONFIG_NXPFMAC_SDIO
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/nxpfmac_sdio_bind.h"
#include "sdio/sdio_device.h"
#endif // CONFIG_NXPFMAC_SDIO

static constexpr zx_driver_ops_t nxpfmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind =
        [](void* ctx, zx_device_t* device) {
#if CONFIG_NXPFMAC_SDIO
          return ::wlan::nxpfmac::SdioDevice::Create(device);
#endif // CONFIG_NXPFMAC_SDIO
        },
};

ZIRCON_DRIVER(nxpfmac, nxpfmac_driver_ops, "zircon", "0.1");
