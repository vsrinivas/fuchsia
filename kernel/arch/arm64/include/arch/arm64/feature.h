// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/features.h>

__BEGIN_CDECLS

extern uint32_t arm64_features;

static inline bool arm64_feature_test(uint32_t feature) {

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
