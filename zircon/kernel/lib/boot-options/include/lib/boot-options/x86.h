// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_X86_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_X86_H_

// This file provides declarations for special types used by x86.inc options.
// See kernel/lib/boot-options/README.md for full details and constraints.
// It should avoid other kernel header dependencies.

// TODO(53594): Wallclock

enum class IntelHwpPolicy {
  // Use BIOS-specified settings if available, falling back to balanced.
  kBiosSpecified,

  // Use high performance, balanaced, or low-power policies respectively.
  kPerformance,
  kBalanced,
  kPowerSave,

  // Use settings that give predictable performance, such as is required
  // for benchmarking.
  kStablePerformance,
};

#define X86_OPTION_TYPES(OPTION_TYPE) OPTION_TYPE(IntelHwpPolicy);

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_X86_H_
