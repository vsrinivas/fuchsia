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

  // Called when a VMO needs to be prepared for use by the data path. The bus needs to ensure that
  // data placed in this VMO can be used for both transmit and receive.
  virtual zx_status_t PrepareVmo(uint8_t vmo_id, zx::vmo&& vmo, uint8_t* mapped_address,
                                 size_t mapped_size) = 0;
  // Called to indicate that a VMO previously prepared by PrepareVmo is no longer needed.
  virtual zx_status_t ReleaseVmo(uint8_t vmo_id) = 0;
  // Called to acquire bus specific headroom needed in RX frames.
  virtual uint16_t GetRxHeadroom() const = 0;
  // Called to acquire bus specific headroom needed in TX frames.
  virtual uint16_t GetTxHeadroom() const = 0;
  // Called to acquire the alignment needed for both RX and TX buffers.
  virtual uint32_t GetBufferAlignment() const = 0;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_BUS_INTERFACE_H_
