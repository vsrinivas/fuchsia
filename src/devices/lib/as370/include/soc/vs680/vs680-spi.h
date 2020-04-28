// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_SPI_H_
#define ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_SPI_H_

#include <stdint.h>

namespace vs680 {

constexpr uint32_t kSpi1Base = 0xf7e8'1c00;
constexpr uint32_t kSpiSize = 0x100;

constexpr uint32_t kSpi1Irq = 82 + 32;

constexpr uint32_t kSpi1Cs0 = 54;
constexpr uint32_t kSpi1Cs1 = 53;
constexpr uint32_t kSpi1Cs2 = 52;
constexpr uint32_t kSpi1Cs3 = 51;
constexpr uint32_t kSpi1Clk = 49;
constexpr uint32_t kSpi1Mosi = 50;
constexpr uint32_t kSpi1Miso = 48;

constexpr uint32_t kSpi1Cs0AltFunction = 0;
constexpr uint32_t kSpi1Cs1AltFunction = 1;
constexpr uint32_t kSpi1Cs2AltFunction = 1;
constexpr uint32_t kSpi1Cs3AltFunction = 1;
constexpr uint32_t kSpi1ClkAltFunction = 0;
constexpr uint32_t kSpi1MosiAltFunction = 0;
constexpr uint32_t kSpi1MisoAltFunction = 0;

}  // namespace vs680

#endif  // ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_I2C_H_
