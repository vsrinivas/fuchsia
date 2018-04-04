// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/fzl/time.h>
#include <lib/zx/time.h>

#include "metrics.h"

namespace minfs {
namespace {

size_t TicksToMs(const zx::ticks& ticks) {
    return fzl::TicksToNs(ticks) / zx::msec(1);
}

} // namespace

void MinfsMetrics::Dump() const {
    constexpr uint64_t KB = 1 << 10;

    printf("Allocation Info:\n");
    printf("  %zu / %zu successful calls to create, total %zu ms\n",
           create_calls_success, create_calls, TicksToMs(create_ticks));
    printf("Operation stats:\n");
    printf("  %zu calls to read totalling %zu KB in %zu ms\n",
           read_calls, read_size / KB, TicksToMs(read_ticks));
    printf("  %zu calls to write totalling %zu KB in %zu ms\n",
           write_calls, write_size / KB, TicksToMs(write_ticks));
    printf("  %zu calls to truncate in %zu ms\n",
           truncate_calls, TicksToMs(truncate_ticks));
    printf("  %zu / %zu successful calls to unlink, total %zu ms\n",
           unlink_calls_success, unlink_calls, TicksToMs(unlink_ticks));
    printf("  %zu / %zu successful calls to rename, total %zu ms\n",
           rename_calls_success, rename_calls, TicksToMs(rename_ticks));
    printf("Lookup stats:\n");
    printf("  %zu initialized VMOs (dnum: %u, inum: %u, dinum: %u)\n",
           initialized_vmos, init_dnum_count, init_inum_count, init_dinum_count);
    printf("  Initialized %zu KB of VMOs in %zu ms\n",
           init_user_data_size / KB, TicksToMs(init_user_data_ticks));
    printf("  %zu / %zu VnodeGet (lookup by inode) cache hits, total %zu ms\n",
           vnodes_opened_cache_hit, vnodes_opened, TicksToMs(vnode_open_ticks));
    printf("  %zu / %zu Lookup (lookup by path) successful calls, %zu ms\n",
           lookup_calls_success, lookup_calls, TicksToMs(lookup_ticks));
}

} // namespace minfs
