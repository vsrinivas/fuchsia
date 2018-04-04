// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Minfs metrics.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/time.h>

namespace minfs {

struct MinfsMetrics {
    // Print information about metrics to stdout.
    //
    // TODO(ZX-1999): This is a stop-gap solution; long-term, this information
    // should be extracted from devices.
    void Dump() const;

    // ALLOCATION STATS
    uint64_t create_calls = 0;
    uint64_t create_calls_success = 0;
    zx::ticks create_ticks = {};

    // OPERATION STATS
    uint64_t read_calls = 0;
    uint64_t read_size = 0;
    zx::ticks read_ticks = {};

    uint64_t write_calls = 0;
    uint64_t write_size = 0;
    zx::ticks write_ticks = {};

    uint64_t truncate_calls = 0;
    zx::ticks truncate_ticks = {};

    uint64_t unlink_calls = 0;
    uint64_t unlink_calls_success = 0;
    zx::ticks unlink_ticks = {};

    uint64_t rename_calls = 0;
    uint64_t rename_calls_success = 0;
    zx::ticks rename_ticks = {};

    // LOOKUP STATS

    // Total time waiting for reads from disk.
    uint64_t initialized_vmos = 0;
    uint32_t init_dnum_count = 0; // Top-level direct blocks only
    uint32_t init_inum_count = 0; // Top-level indirect blocks only
    uint32_t init_dinum_count = 0;
    uint64_t init_user_data_size = 0;
    zx::ticks init_user_data_ticks = {};

    // Opened via "VnodeGet".
    uint64_t vnodes_opened = 0;
    uint64_t vnodes_opened_cache_hit = 0;
    zx::ticks vnode_open_ticks = {};

    // Opened via "LookupInternal".
    uint64_t lookup_calls = 0;
    uint64_t lookup_calls_success = 0;
    zx::ticks lookup_ticks = {};

    // FVM STATS
    // TODO(smklein)
};

} // namespace minfs
