// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_FLASH_CHIPS_H_
#define SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_FLASH_CHIPS_H_

#include <zircon/types.h>

#include <string_view>
namespace spiflash {

inline constexpr uint16_t kVendorGigadevice = 0xc8;
inline constexpr uint16_t kDeviceGigadeviceGD25Q127C = 0x4018;

struct FlashChipInfo {
  std::string_view name;
  uint16_t vendor_id;
  uint16_t device_id;
  uint32_t page_size;
  uint64_t size;
};

inline constexpr FlashChipInfo kFlashDevices[] = {
    {
        .name = "GigaDevice GD25Q127C",
        .vendor_id = kVendorGigadevice,
        .device_id = kDeviceGigadeviceGD25Q127C,
        .page_size = 256,
        .size = 16777216,
    },
};

}  // namespace spiflash

#endif  // SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_FLASH_CHIPS_H_
