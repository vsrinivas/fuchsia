// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>

#include "bench.h"

// spin the cpu a bit to make sure the frequency is cranked to the top
static void spin(mx_time_t nanosecs) {
    mx_time_t t = mx_time_get(MX_CLOCK_MONOTONIC);

    while (mx_time_get(MX_CLOCK_MONOTONIC) - t < nanosecs)
        ;
}

template <typename T>
inline mx_time_t time_it(T func) {
    spin(MX_MSEC(10));

    mx_time_t t = mx_time_get(MX_CLOCK_MONOTONIC);
    func();
    return mx_time_get(MX_CLOCK_MONOTONIC) - t;
}

int vmo_run_benchmark() {
    mx_time_t t;
    //mx_handle_t vmo;

    printf("starting VMO benchmark\n");

    // allocate a bunch of large vmos, delete them
    const size_t size = 32*1024*1024;
    mx_handle_t vmos[32];
    uintptr_t ptr;

    t = time_it([&](){
        for (auto& vmo : vmos) {
            mx_vmo_create(size, 0, &vmo);
        }
    });

    printf("\ttook %" PRIu64 " nsecs to create %zu vmos of size %zu\n", t, fbl::count_of(vmos), size);

    t = time_it([&](){
        for (auto& vmo : vmos) {
            mx_handle_close(vmo);
        }
    });
    printf("\ttook %" PRIu64 " nsecs to delete %zu vmos of size %zu\n", t, fbl::count_of(vmos), size);

    // create a vmo and demand fault it in
    mx_handle_t vmo;
    mx_vmo_create(size, 0, &vmo);

    mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);

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
        mx_vmar_unmap(mx_vmar_root_self(), ptr, size);
    });
    printf("\ttook %" PRIu64 " nsecs to unmap the vmo %zu (%zu pages)\n", t, size, size / PAGE_SIZE);

    // map it a again and time read faulting it
    mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            __UNUSED char a = ((volatile char *)ptr)[i];
        }
    });
    printf("\ttook %" PRIu64 " nsecs to read fault in vmo of size %zu in another mapping\n", t, size);

    mx_vmar_unmap(mx_vmar_root_self(), ptr, size);

    // map it a again and time write faulting it
    mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu in another mapping\n", t, size);

    mx_vmar_unmap(mx_vmar_root_self(), ptr, size);

    // delete the vmo
    t = time_it([&](){
        mx_handle_close(vmo);
    });
    printf("\ttook %" PRIu64 " nsecs to delete populated vmo of size %zu\n", t, size);

    // create a second vmo and write fault it in directly
    mx_vmo_create(size, 0, &vmo);

    mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            ((volatile char *)ptr)[i] = 99;
        }
    });
    printf("\ttook %" PRIu64 " nsecs to write fault in vmo of size %zu\n", t, size);

    mx_handle_close(vmo);

    // create a vmo and commit and decommit it directly
    mx_vmo_create(size, 0, &vmo);

    t = time_it([&](){
        mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    });
    printf("\ttook %" PRIu64 " nsecs to commit vmo of size %zu\n", t, size);

    uint64_t addrs[size / PAGE_SIZE];
    t = time_it([&](){
        mx_status_t status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, size, addrs, sizeof(addrs));
        if (status != MX_OK) {
            __builtin_trap();
        }
    });
    printf("\ttook %" PRIu64 " nsecs to lookup vmo of size %zu\n", t, size);

    t = time_it([&](){
        mx_status_t status = mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, 0, size, nullptr, 0);
        if (status != MX_OK) {
            __builtin_trap();
        }
    });
    printf("\ttook %" PRIu64 " nsecs to commit already committed vmo of size %zu\n", t, size);

    t = time_it([&](){
        mx_vmo_op_range(vmo, MX_VMO_OP_DECOMMIT, 0, size, nullptr, 0);
    });
    printf("\ttook %" PRIu64 " nsecs to decommit vmo of size %zu\n", t, size);

    mx_handle_close(vmo);

    printf("done with benchmark\n");

    return 0;
}
