// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// feature flags
// clang-format off
#define ARM64_FEATURE_ISA_FP        (1u << 0)
#define ARM64_FEATURE_ISA_ASIMD     (1u << 1)
#define ARM64_FEATURE_ISA_AES       (1u << 2)
#define ARM64_FEATURE_ISA_PMULL     (1u << 3)
#define ARM64_FEATURE_ISA_SHA1      (1u << 4)
#define ARM64_FEATURE_ISA_SHA2      (1u << 5)
#define ARM64_FEATURE_ISA_CRC32     (1u << 6)
#define ARM64_FEATURE_ISA_ATOMICS   (1u << 7)
#define ARM64_FEATURE_ISA_RDM       (1u << 8)
#define ARM64_FEATURE_ISA_SHA3      (1u << 9)
#define ARM64_FEATURE_ISA_SM3       (1u << 10)
#define ARM64_FEATURE_ISA_SM4       (1u << 11)
#define ARM64_FEATURE_ISA_DP        (1u << 12)
#define ARM64_FEATURE_ISA_DPB       (1u << 13)
// clang-format on

static inline bool arm64_feature_test(uint32_t feature) {
    extern uint32_t arm64_features;

    return arm64_features & feature;
}

/* block size of the dc zva instruction, dcache cache line and icache cache line */
extern uint32_t arm64_zva_size;
extern uint32_t arm64_icache_size;
extern uint32_t arm64_dcache_size;

// call on every cpu to initialize the feature set
void arm64_feature_init(void);

// dump the feature set
void arm64_feature_debug(bool full);

void arm64_get_cache_info(arm64_cache_info_t* info);
void arm64_dump_cache_info(uint32_t cpu);

__END_CDECLS
