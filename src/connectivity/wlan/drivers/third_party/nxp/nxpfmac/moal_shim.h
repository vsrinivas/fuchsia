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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MOAL_SHIM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MOAL_SHIM_H_

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

// Populate common moal callbacks in mlan_device.callbacks. Note that this does NOT populate bus
// specific callbacks such as read_reg, write_reg for SDIO or moal_map_memory, moal_unmap_memory for
// PCIe. These callbacks will have to be provided by the bus layer. This function will not touch
// those callbacks in case they have already been set when this function is called.
void populate_callbacks(mlan_device* mlan_dev);

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MOAL_SHIM_H_
