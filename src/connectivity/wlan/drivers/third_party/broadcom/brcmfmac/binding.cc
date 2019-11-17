// Copyright (c) 2019 The Fuchsia Authors
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

#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <hw/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"

#if CONFIG_BRCMFMAC_PCIE
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_device.h"  // nogncheck
#endif  // CONFIG_BRCMFMAC_PCIE
#if CONFIG_BRCMFMAC_SDIO
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio_device.h"  // nogncheck
#endif  // CONFIG_BRCMFMAC_SDIO
#if CONFIG_BRCMFMAC_DRIVER_TEST
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_driver_test.h"  // nogncheck
#endif  // CONFIG_BRCMFMAC_DRIVER_TEST

static constexpr zx_driver_ops_t brcmfmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind =
        [](void* ctx, zx_device_t* device) {
          zx_status_t status = ZX_ERR_NOT_SUPPORTED;
#if CONFIG_BRCMFMAC_PCIE
          {
            ::wlan::brcmfmac::PcieDevice* pcie_device = nullptr;
            if ((status = ::wlan::brcmfmac::PcieDevice::Create(device, &pcie_device)) == ZX_OK) {
              return ZX_OK;
            }
            if (status != ZX_ERR_NOT_SUPPORTED) {
              return status;
            }
          }
#endif  // CONFIG_BRCMFMAC_PCIE
#if CONFIG_BRCMFMAC_SDIO
          if ((status = ::wlan::brcmfmac::SdioDevice::Create(device)) == ZX_OK) {
            return ZX_OK;
          }
          if (status != ZX_ERR_NOT_SUPPORTED) {
            return status;
          }
#endif  // CONFIG_BRCMFMAC_SDIO
          return status;
        },
#if CONFIG_BRCMFMAC_DRIVER_TEST
    .run_unit_tests =
        [](void* ctx, zx_device_t* parent, zx_handle_t channel) {
          bool retval = true;
          retval &= (::wlan::brcmfmac::RunPcieDriverTest(parent) == ZX_OK);
          return retval;
        },
#endif  // CONFIG_BRCMFMAC_DRIVER_TESTS
};

// clang-format off

ZIRCON_DRIVER_BEGIN(brcmfmac, brcmfmac_driver_ops, "zircon", "0.1", 33)
#if CONFIG_BRCMFMAC_SDIO
    BI_GOTO_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE, 5910),
#endif  // CONFIG_BRCMFMAC_SDIO
#if CONFIG_BRCMFMAC_PCIE
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, BRCM_PCIE_VENDOR_ID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, PCI_CLASS_NETWORK),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, PCI_SUBCLASS_NETWORK_OTHER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4350_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4356_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43567_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43570_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4358_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4359_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_RAW_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_5G_DEVICE_ID),
    // TODO(cphoenix): support this chipset.
    // BRCMF_PCIE_DEVICE_SUB(0x4365, BRCM_PCIE_VENDOR_ID_BROADCOM, 0x4365),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4371_DEVICE_ID),
    BI_ABORT(),
#endif  // CONFIG_BRCMFMAC_PCIE
#if CONFIG_BRCMFMAC_SDIO
    // Composite binding used for SDIO.
    BI_LABEL(5910),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_BCM_WIFI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM4356),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM43458),
#endif  // CONFIG_BRCMFMAC_SDIO
ZIRCON_DRIVER_END(brcmfmac)
