// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_FIRMWARE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_FIRMWARE_H_

#include <zircon/types.h>

#include <string>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"

namespace wlan {
namespace brcmfmac {

class Device;

// Get the firmware binary for the given bus and chip, as a data string.
zx_status_t GetFirmwareBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chip_id,
                              uint32_t chip_rev, std::string* binary_out);

// Get the CLM binary blob for the given bus and chip, as a data string.
zx_status_t GetClmBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chip_id,
                         uint32_t chip_rev, std::string* binary_out);

// Get the NVRAM binary for the given bus and chip, as a data string.  The returned binary has
// already beedn parsed and is suitable for uploading to the device.
zx_status_t GetNvramBinary(Device* device, brcmf_bus_type bus_type, CommonCoreId chip_id,
                           uint32_t chip_rev, std::string* binary_out);

// Parse an NVRAM image from file, into a format suitable for uploading to the device.  This
// function is exposed here for testing.
zx_status_t ParseNvramBinary(std::string_view nvram, std::string* parsed_nvram_out);

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_FIRMWARE_H_
