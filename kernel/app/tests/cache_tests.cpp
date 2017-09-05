// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <arch.h>
#include <arch/ops.h>
#include <inttypes.h>
#include <lib/console.h>
#include <platform.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bench_cache(size_t bufsize, uint8_t* buf) {
    lk_time_t t;
    bool do_free;

    if (buf == 0) {
        buf = (uint8_t*)memalign(PAGE_SIZE, bufsize);
        do_free = true;
    } else {
        do_free = false;
    }

    printf("buf %p, size %zu\n", buf, bufsize);

    if (!buf)
        return;

    t = current_time();
    arch_clean_cache_range((addr_t)buf, bufsize);
    t = current_time() - t;

    printf("took %" PRIu64 " nsecs to clean %zu bytes (cold)\n", t, bufsize);

    memset(buf, 0x99, bufsize);

    t = current_time();
    arch_clean_cache_range((addr_t)buf, bufsize);
    t = current_time() - t;

    if (do_free)
        free(buf);

    printf("took %" PRIu64 " nsecs to clean %zu bytes (hot)\n", t, bufsize);
}

static int cache_tests(int argc, const cmd_args* argv, uint32_t flags) {
    uint8_t* buf;
    buf = (uint8_t*)((argc > 1) ? argv[1].u : 0UL);

    printf("testing cache\n");

    bench_cache(2 * 1024, buf);
    bench_cache(64 * 1024, buf);
    bench_cache(256 * 1024, buf);
    bench_cache(1 * 1024 * 1024, buf);
    bench_cache(8 * 1024 * 1024, buf);
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("cache_tests", "test/bench the cpu cache", &cache_tests)
STATIC_COMMAND_END(cache_tests);
