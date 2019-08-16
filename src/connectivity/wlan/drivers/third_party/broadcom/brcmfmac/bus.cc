// Copyright (c) 2019 The Fuchsia Authors.
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice appear
// in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

// Note that this file does not include any headers that define the bus-specific functions it calls,
// since it cannot depend on them.  Hence we just declare them directly before use.

zx_status_t brcmf_bus_register(brcmf_pub* drvr, std::unique_ptr<brcmf_bus>* out_bus) {
#if CONFIG_BRCMFMAC_SDIO
  {
    extern zx_status_t brcmf_sdio_register(brcmf_pub * drvr, std::unique_ptr<brcmf_bus> * out_bus);
    const zx_status_t result = brcmf_sdio_register(drvr, out_bus);
    if (result != ZX_OK) {
      BRCMF_DBG(INFO, "SDIO registration failed: %d\n", result);
    } else {
      return ZX_OK;
    }
  }
#endif  // CONFIG_BRCMFMAC_SDIO

#if CONFIG_BRCMFMAC_SIM
  {
    extern zx_status_t brcmf_sim_register(brcmf_pub * drvr, std::unique_ptr<brcmf_bus> * out_bus);
    const zx_status_t result = brcmf_sim_register(drvr, out_bus);
    if (result != ZX_OK) {
      BRCMF_DBG(INFO, "SIM registration failed: %d\n", result);
    } else {
      return ZX_OK;
    }
  }
#endif  // CONFIG_BRCMFMAC_SIM

  return ZX_ERR_NOT_SUPPORTED;
}

void brcmf_bus_exit(brcmf_bus* bus) {
#if CONFIG_BRCMFMAC_SDIO
  extern void brcmf_sdio_exit(brcmf_bus * bus);
  brcmf_sdio_exit(bus);
#endif

#if CONFIG_BRCMFMAC_SIM
  extern void brcmf_sim_exit(brcmf_bus * bus);
  brcmf_sim_exit(bus);
#endif
}
