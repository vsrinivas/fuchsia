// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_FENV_ARM_ACLE_H_
#define ZIRCON_SYSTEM_ULIB_C_FENV_ARM_ACLE_H_

// This header exists to provide arm intrinsics that gcc does not provide.
// These are specified by the arm spec and gcc should include them.
// TODO(fxbug.dev/102847): Remove this after gcc adds these intrinsics.

#ifdef __clang__
#error "clang already exposes these"
#endif

#include_next <arm_acle.h>

// Read 32-bit system register.
#define __arm_rsr(reg)                            \
  ({                                              \
    uint32_t _val;                                \
    __asm__ volatile("mrs %0," reg : "=r"(_val)); \
    _val;                                         \
  })

// Write 32-bit system register.
#define __arm_wsr(reg, val)                          \
  ({                                                 \
    uint32_t _val = (val);                           \
    __asm__ volatile("msr " reg ", %0" ::"r"(_val)); \
  })

#endif  // ZIRCON_SYSTEM_ULIB_C_FENV_ARM_ACLE_H_
