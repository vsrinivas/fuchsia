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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_BUS_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_BUS_INTERFACE_H_

#include <lib/zx/vmo.h>

namespace wlan::nxpfmac {

class IoctlAdapter;

class BusInterface {
 public:
  virtual ~BusInterface() = default;

  // Called when the device has registered with MLAN. Any error will stop the device from binding.
  virtual zx_status_t OnMlanRegistered(void* mlan_adapter) = 0;
  // Called when firmware has been successfully initialized. Any error will stop the device from
  // binding.
  virtual zx_status_t OnFirmwareInitialized() = 0;

  // Trigger the mlan main process, useful for things that return a pending status such as ioctls.
  virtual zx_status_t TriggerMainProcess() = 0;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_BUS_INTERFACE_H_
