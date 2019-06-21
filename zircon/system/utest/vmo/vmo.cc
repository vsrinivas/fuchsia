// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <threads.h>

#include <fbl/function.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "bench.h"

bool vmo_cache_map_test() {
    BEGIN_TEST;

    auto maptest = [](uint32_t policy, const char *type) {
        zx_handle_t vmo;
        const size_t size = 256*1024; // 256K

        EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo));

        // set the cache policy
        EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, policy));

        // commit it
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0));

        // map it
        uintptr_t ptr;
        EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(),
                  ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE,
                  0, vmo, 0, size, &ptr));

        volatile uint32_t *buf = (volatile uint32_t *)ptr;

        // write it once, priming the cache
        for (size_t i = 0; i < size / 4; i++)
            buf[i] = 0;

        // write to it
        zx_time_t wt = zx_clock_get_monotonic();
        for (size_t i = 0; i < size / 4; i++)
            buf[i] = 0;
        wt = zx_clock_get_monotonic() - wt;

        // read from it
        zx_time_t rt = zx_clock_get_monotonic();
        for (size_t i = 0; i < size / 4; i++)
            __UNUSED uint32_t hole = buf[i];
        rt = zx_clock_get_monotonic() - rt;

        printf("took %" PRIu64 " nsec to write %s memory\n", wt, type);
        printf("took %" PRIu64 " nsec to read %s memory\n", rt, type);

        EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size));
        EXPECT_EQ(ZX_OK, zx_handle_close(vmo));
    };

    printf("\n");
    maptest(ZX_CACHE_POLICY_CACHED, "cached");
    maptest(ZX_CACHE_POLICY_UNCACHED, "uncached");
    maptest(ZX_CACHE_POLICY_UNCACHED_DEVICE, "uncached device");
    maptest(ZX_CACHE_POLICY_WRITE_COMBINING, "write combining");

    END_TEST;
}

bool vmo_unmap_coherency() {
    BEGIN_TEST;

    // This is an expensive test to try to detect a multi-cpu coherency
    // problem with TLB flushing of unmap operations
    //
    // algorithm: map a relatively large committed VMO.
    // Create a worker thread that simply walks through the VMO writing to
    // each page.
    // In the main thread continually decommit the vmo with a little bit of
    // a gap between decommits to allow the worker thread to bring it all back in.
    // If the worker thread appears stuck by not making it through a loop in
    // a reasonable time, we have failed.

    // allocate a vmo
    const size_t len = 32*1024*1024;
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // do a regular map
    uintptr_t ptr = 0;
    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         0, vmo, 0, len, &ptr);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr, "map address");

    // create a worker thread
    struct worker_args {
        size_t len;
        uintptr_t ptr;
        std::atomic<bool> exit;
        std::atomic<bool> exited;
        std::atomic<size_t> count;
    } args = {};
    args.len = len;
    args.ptr = ptr;

    auto worker = [](void *_args) -> int {
        worker_args* a = (worker_args*)_args;

        unittest_printf("ptr %#" PRIxPTR " len %zu\n",
                a->ptr, a->len);

        while (!a->exit.load()) {
            // walk through the mapping, writing to every page
            for (size_t off = 0; off < a->len; off += PAGE_SIZE) {
                *(uint32_t*)(a->ptr + off) = 99;
            }

            a->count.fetch_add(1);
        }

        unittest_printf("exiting worker\n");

        a->exited.store(true);

        return 0;
    };

    thrd_t t;
    thrd_create(&t, worker, &args);

    const zx_time_t max_duration = ZX_SEC(30);
    const zx_time_t max_wait = ZX_SEC(1);
    zx_time_t start = zx_clock_get_monotonic();
    for (;;) {
        // wait for it to loop at least once
        zx_time_t t = zx_clock_get_monotonic();
        size_t last_count = args.count.load();
        while (args.count.load() <= last_count) {
            if (zx_clock_get_monotonic() - t > max_wait) {
                UNITTEST_FAIL_TRACEF("looper appears stuck!\n");
                break;
            }
        }

        // decommit the vmo
        status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, len, nullptr, 0);
        EXPECT_EQ(0, status, "vm decommit");

        if (zx_clock_get_monotonic() - start > max_duration)
            break;
    }

    // stop the thread and wait for it to exit
    args.exit.store(true);
    while (args.exited.load() == false)
        ;

    END_TEST;
}

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST_PERFORMANCE(vmo_cache_map_test);
RUN_TEST_LARGE(vmo_unmap_coherency);
END_TEST_CASE(vmo_tests)

int main(int argc, char** argv) {
    bool run_bench = false;
    if (argc > 1) {
        if (!strcmp(argv[1], "bench")) {
            run_bench = true;
        }
    }

    if (!run_bench) {
        bool success = unittest_run_all_tests(argc, argv);
        return success ? 0 : -1;
    } else {
        return vmo_run_benchmark();
    }
}
