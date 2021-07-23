// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_REGISTERS_H_
#define SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_REGISTERS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

#include "hwreg/internal.h"

namespace spiflash {

inline constexpr uint32_t kSpiFlashBfpreg = 0x00;
inline constexpr uint32_t kSpiFlashHfstsCtl = 0x04;
inline constexpr uint32_t kSpiFlashFaddr = 0x08;
inline constexpr uint32_t kSpiFlashDlock = 0x0c;
inline constexpr uint32_t kSpiFlashFdataBase = 0x10;
inline constexpr uint32_t kSpiFlashFdataCount = 16;
inline constexpr uint32_t kSpiFlashFracc = 0x50;
inline constexpr uint32_t kSpiFlashFregBase = 0x54;
inline constexpr uint32_t kSpiFlashFregCount = 5;
inline constexpr uint32_t kSpiFlashFprBase = 0x84;
inline constexpr uint32_t kSpiFlashFprCount = 5;
inline constexpr uint32_t kSpiFlashGprBase = 0x98;
inline constexpr uint32_t kSpiFlashSfracc = 0xb0;
inline constexpr uint32_t kSpiFlashFdoc = 0xb4;
inline constexpr uint32_t kSpiFlashFdod = 0xb8;
inline constexpr uint32_t kSpiFlashAfc = 0xc0;
inline constexpr uint32_t kSpiFlashVscc0 = 0xc4;
inline constexpr uint32_t kSpiFlashVscc1 = 0xc8;
inline constexpr uint32_t kSpiFlashPtinx = 0xcc;
inline constexpr uint32_t kSpiFlashPtdata = 0xd0;
inline constexpr uint32_t kSpiFlashSbrs = 0xd4;

class FlashControl : public hwreg::RegisterBase<FlashControl, uint32_t, hwreg::EnablePrinter> {
 public:
  enum CycleType {
    kRead = 0x0,
    kWrite = 0x2,
    kErase4k = 0x3,
    kErase64k = 0x4,
    kReadSfdp = 0x5,
    kReadJedecId = 0x6,
    kWriteStatus = 0x7,
    kReadStatus = 0x8,
    kRpmcOp1 = 0x9,
    kRpmcOp2 = 0xa,
  };

  DEF_BIT(31, fsmie);
  DEF_FIELD(29, 24, fdbc);
  DEF_BIT(21, wet);
  DEF_ENUM_FIELD(CycleType, 20, 17, fcycle);
  DEF_BIT(16, fgo);
  DEF_BIT(15, flockdn);
  DEF_BIT(14, fdv);
  DEF_BIT(13, fdopss);
  DEF_BIT(12, prr34_lockdn);
  DEF_BIT(11, wrsdis);
  DEF_BIT(5, h_scip);
  DEF_BIT(2, h_ael);
  DEF_BIT(1, fcerr);
  DEF_BIT(0, fdone);

  static auto Get() { return hwreg::RegisterAddr<FlashControl>(kSpiFlashHfstsCtl); }
};

class FlashAddress : public hwreg::RegisterBase<FlashAddress, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(26, 0, fla);

  static auto Get() { return hwreg::RegisterAddr<FlashControl>(kSpiFlashFaddr); }
};

class FlashData : public hwreg::RegisterBase<FlashData, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 0, data);

  static auto Get(uint32_t which) {
    ZX_ASSERT(which < kSpiFlashFdataCount);
    return hwreg::RegisterAddr<FlashData>(kSpiFlashFdataBase + (4 * which));
  }
};

}  // namespace spiflash

#endif  // SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_REGISTERS_H_
