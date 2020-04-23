// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_H_
#define SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_H_

static constexpr uint32_t kPmicArbCoreMmio = 0x200f000;
static constexpr uint32_t kPmicArbCoreMmioSize = 0x1000;
static constexpr uint32_t kPmicArbChnlsMmio = 0x2400000;
static constexpr uint32_t kPmicArbChanlsMmioSize = 0x800000;
static constexpr uint32_t kPmicArbObsvrMmio = 0x2c00000;
static constexpr uint32_t kPmicArbObsvrMmioSize = 0x800000;
static constexpr uint32_t kPmicArbIntrMmio = 0x3800000;
static constexpr uint32_t kPmicArbIntrMmioSize = 0x200000;
static constexpr uint32_t kPmicArbCnfgMmio = 0x200a000;
static constexpr uint32_t kPmicArbCnfgMmioSize = 0x2100;
static constexpr uint32_t kMaxPmicPeripherals = 256;

// TODO(ravoorir): Is power domain right
// terminology here, given that this includes register
// writes
enum QComPowerDomains {
  kVRegS1,
  kVRegS2,
  kVRegS3,
  kVRegS4,
  kVRegS5,
  kVRegS6,
  kVRegS7,
  kVRegLdoA1,
  kVRegLdoA2,
  kVRegLdoA3,
  kVRegLdoA5,
  kVRegLdoA6,
  kVRegLdoA7,
  kVRegLdoA8,
  kVRegLdoA9,
  kVRegLdoA10,
  kVRegLdoA11,
  kVRegLdoA12,
  kVRegLdoA13,
  kVRegLdoA16,
  kVRegLdoA17,
  kVRegLdoA19,
  kVRegLdoA22,
  kVRegLdoA23,
  kPmicCtrlReg,
};

#endif  // SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_POWER_H_
