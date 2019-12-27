// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_FIRMWARE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_FIRMWARE_H_

#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <string>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"

namespace wlan {
namespace brcmfmac {

class Device;
class PcieBuscore;

// This class encapsulates the brcmfmac firmware loading functionality over PCIE.  Upon successful
// creation, the firmware will be loaded and correctly running.  The class also exposes
// firmware-related configuration information that is passed back to the driver through a
// host/device shared memory block.
class PcieFirmware {
 public:
  PcieFirmware();
  ~PcieFirmware();

  // Static factory function for PcieFirmware instances.
  static zx_status_t Create(Device* device, PcieBuscore* buscore,
                            std::unique_ptr<PcieFirmware>* out_firmware);

  // Accessors for various shared RAM states.
  uint8_t GetSharedRamVersion() const;
  uint16_t GetSharedRamFlags() const;
  uint32_t GetDeviceToHostMailboxDataAddress() const;

  // Read the firmware console.  Only complete lines are returned, one at a time.
  std::string ReadConsole();

 private:
  struct SharedRamInfo;

  PcieBuscore* buscore_ = nullptr;
  std::unique_ptr<SharedRamInfo> shared_ram_info_;

  // Firmware scratch buffers.
  std::unique_ptr<DmaBuffer> dma_d2h_scratch_buffer_;
  std::unique_ptr<DmaBuffer> dma_d2h_ringupdate_buffer_;

  std::string console_line_;
  uint32_t console_buffer_addr_ = 0;
  uint32_t console_buffer_size_ = 0;
  uint32_t console_read_index_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_FIRMWARE_H_
