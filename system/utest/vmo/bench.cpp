// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <fbl/algorithm.h>

#include "bench.h"

static zx_ticks_t ns_to_ticks(zx_time_t ns) {
    __uint128_t temp = (__uint128_t)ns * zx_ticks_per_second() / ZX_SEC(1);
    return (zx_ticks_t)temp;
}

static zx_time_t ticks_to_ns(zx_ticks_t ticks) {
    __uint128_t temp = (__uint128_t)ticks * ZX_SEC(1) / zx_ticks_per_second();
    return (zx_time_t)temp;
}

// spin the cpu a bit to make sure the frequency is cranked to the top
static void spin(zx_time_t nanosecs) {
    zx_ticks_t target_ticks = ns_to_ticks(nanosecs);
    zx_ticks_t t = zx_ticks_get();

    while (zx_ticks_get() - t < target_ticks)
        ;
}

template <typename T>
inline zx_time_t time_it(T func) {
    spin(ZX_MSEC(10));

    zx_ticks_t ticks = zx_ticks_get();
    func();
    ticks = zx_ticks_get() - ticks;

    return ticks_to_ns(ticks);
}

int vmo_run_benchmark() {
    zx_time_t t;
    //zx_handle_t vmo;

    printf("starting VMO benchmark\n");

    // allocate a bunch of large vmos, delete them
    const size_t size = 32*1024*1024;
    zx_handle_t vmos[32];
    uintptr_t ptr;

    t = time_it([&](){
        for (auto& vmo : vmos) {
            zx_vmo_create(size, 0, &vmo);
        }
    });

    printf("\ttook %" PRIu64 " nsecs to create %zu vmos of size %zu\n", t, fbl::count_of(vmos), size);

    t = time_it([&](){
        for (auto& vmo : vmos) {
            zx_handle_close(vmo);
        }
    });
    printf("\ttook %" PRIu64 " nsecs to delete %zu vmos of size %zu\n", t, fbl::count_of(vmos), size);

    // create a vmo and demand fault it in
    zx_handle_t vmo;
    zx_vmo_create(size, 0, &vmo);

    zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            __UNUSED char a = ((volatile char *)ptr)[i];
        }
    });
    printf("\ttook %" PRIu64 " nsecs to read fault in vmo of size %zu (should be read faulting a zero page)\n", t, size);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            __UNUSED char a = ((volatile char *)ptr)[i];
        }
    });
    printf("\ttook %" PRIu64 " nsecs to read in vmo of size %zu a second time (should be mapped already)\n", t, size);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu after read faulting it\n", t, size);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu a second time\n", t, size);

    // unmap the original mapping
    t = time_it([&](){
        zx_vmar_unmap(zx_vmar_root_self(), ptr, size);
    });
    printf("\ttook %" PRIu64 " nsecs to unmap the vmo %zu (%zu pages)\n", t, size, size / PAGE_SIZE);

    // map it a again and time read faulting it
    zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            __UNUSED char a = ((volatile char *)ptr)[i];
        }
    });
    printf("\ttook %" PRIu64 " nsecs to read fault in vmo of size %zu in another mapping\n", t, size);

    zx_vmar_unmap(zx_vmar_root_self(), ptr, size);

    // map it a again and time write faulting it
    zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu in another mapping\n", t, size);

    zx_vmar_unmap(zx_vmar_root_self(), ptr, size);

    // delete the vmo
    t = time_it([&](){
        zx_handle_close(vmo);
    });
    printf("\ttook %" PRIu64 " nsecs to delete populated vmo of size %zu\n", t, size);

    // create a second vmo and write fault it in directly
    zx_vmo_create(size, 0, &vmo);

    zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu\n", t, size);

    zx_handle_close(vmo);

    // create a vmo and commit and decommit it directly
    zx_vmo_create(size, 0, &vmo);

    t = time_it([&](){
        zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    });
    printf("\ttook %" PRIu64 " nsecs to commit vmo of size %zu\n", t, size);

    t = time_it([&](){
        zx_status_t status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
        if (status != ZX_OK) {
            __builtin_trap();
        }
    });
    printf("\ttook %" PRIu64 " nsecs to commit already committed vmo of size %zu\n", t, size);

    t = time_it([&](){
        zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, size, nullptr, 0);
    });
    printf("\ttook %" PRIu64 " nsecs to decommit vmo of size %zu\n", t, size);

    zx_handle_close(vmo);

    printf("done with benchmark\n");

    return 0;
}
