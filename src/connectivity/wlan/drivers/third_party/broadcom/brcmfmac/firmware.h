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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_

#include <zircon/types.h>

#include <string>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"

namespace wlan {
namespace brcmfmac {

class Device;

// Get the firmware binary for the given bus and chip, as a data string.
zx_status_t GetFirmwareBinary(Device* device, brcmf_bus_type bus_type, uint32_t chipid,
                              uint32_t chiprev, std::string* binary_out);

// Get the CLM binary blob for the given bus and chip, as a data string.
zx_status_t GetClmBinary(Device* device, brcmf_bus_type bus_type, uint32_t chipid, uint32_t chiprev,
                         std::string* binary_out);

// Get the NVRAM binary for the given bus and chip, as a data string.  The returned binary has
// already beedn parsed and is suitable for uploading to the device.
zx_status_t GetNvramBinary(Device* device, brcmf_bus_type bus_type, uint32_t chipid,
                           uint32_t chiprev, std::string* binary_out);

// Parse an NVRAM image from file, into a format suitable for uploading to the device.  This
// function is exposed here for testing.
zx_status_t ParseNvramBinary(std::string_view nvram, std::string* parsed_nvram_out);

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FIRMWARE_H_
