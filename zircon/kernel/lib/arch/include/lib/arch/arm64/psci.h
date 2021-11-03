// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_PSCI_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_PSCI_H_

#include <stddef.h>
#include <stdint.h>

namespace arch {

// [arm/psci] 5.2.1: Register usage in arguments and return values
// The function ID is passed in x0, with arguments in x1, x2, and x3.
// A value is returned in x0.
constexpr size_t kArmPsciRegisters = 4;

// [arm/psci] 5.1: Function prototypes
// These are the SMC64 versions when both SMC32 and SMC64 versions are defined.
enum class ArmPsciFunction : uint64_t {
  kPsciVersion = 0x8400'0000,
  kCpuSuspend = 0xc400'0001,
  kCpuOff = 0x8400'0002,
  kCpuOn = 0xc400'0003,
  kAffinityInfo = 0xc400'0004,
  kMigrate = 0xc400'0005,
  kMigrateInfoType = 0x8400'0006,
  kMigrateInfoUpCpu = 0xc400'0007,
  kSystemOff = 0x8400'0008,
  kSystemReset = 0x8400'0009,
  kSystemReset2 = 0xc400'0012,
  kMemProtect = 0x8400'0013,
  kMemProtectCheckRange = 0xc400'0014,
  kPsciFeatures = 0x8400'000a,
  kCpuFreeze = 0x8400'000b,
  kCpuDefaultSuspend = 0x8400'000c,
  kNodeHwState = 0x8400'000d,
  kSystemSuspend = 0xc400'000e,
  kPsciSetSuspendMode = 0x8400'000f,
  kPsciStatResidency = 0xc400'0010,
  kPsciStatCount = 0xc400'0011,
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_PSCI_H_
