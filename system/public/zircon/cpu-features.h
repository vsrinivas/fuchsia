// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
// clang-format off


// First flag is arch-independent and indicates if |zx_system_get_cpu_features|
// is supported.
#define ZX_CPU_HAS_FEATURES            (1u << 0)

// Architecture-specific feature flags that can be returned by
// |zx_system_get_cpu_features|.
#if defined(__x86_64__)

// None; use cpuid instead

#elif defined(__aarch64__)

#define ZX_ARM64_FEATURE_ISA_FP        (1u << 1)
#define ZX_ARM64_FEATURE_ISA_ASIMD     (1u << 2)
#define ZX_ARM64_FEATURE_ISA_AES       (1u << 3)
#define ZX_ARM64_FEATURE_ISA_PMULL     (1u << 4)
#define ZX_ARM64_FEATURE_ISA_SHA1      (1u << 5)
#define ZX_ARM64_FEATURE_ISA_SHA2      (1u << 6)
#define ZX_ARM64_FEATURE_ISA_CRC32     (1u << 7)
#define ZX_ARM64_FEATURE_ISA_ATOMICS   (1u << 8)
#define ZX_ARM64_FEATURE_ISA_RDM       (1u << 9)
#define ZX_ARM64_FEATURE_ISA_SHA3      (1u << 10)
#define ZX_ARM64_FEATURE_ISA_SM3       (1u << 11)
#define ZX_ARM64_FEATURE_ISA_SM4       (1u << 12)
#define ZX_ARM64_FEATURE_ISA_DP        (1u << 13)
#define ZX_ARM64_FEATURE_ISA_DPB       (1u << 14)

#else

#error what architecture?

#endif
