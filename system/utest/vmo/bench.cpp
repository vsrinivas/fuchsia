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
#include <magenta/syscalls.h>

#include "bench.h"

template <typename T>
inline mx_time_t time_it(T func) {
    mx_time_t t = mx_current_time();
    func();
    return mx_current_time() - t;
}

int vmo_run_benchmark() {
    mx_time_t t;
    //mx_handle_t vmo;

    printf("starting VMO benchmark\n");

    // allocate a bunch of large vmos, delete them
    const size_t size = 32*1024*1024;
    mx_handle_t vmos[32];

    t = time_it([&](){
        for (auto& vmo : vmos) {
            vmo = mx_vmo_create(size);
        }
    });

    printf("\ttook %" PRIu64 " nsecs to create %zu vmos of size %zu\n", t, countof(vmos), size);

    t = time_it([&](){
        for (auto& vmo : vmos) {
            mx_handle_close(vmo);
        }
    });
    printf("\ttook %" PRIu64 " nsecs to delete %zu vmos of size %zu\n", t, countof(vmos), size);

    // create a vmo and demand fault it in
    auto &vmo = vmos[0];

    vmo = mx_vmo_create(size);

    uintptr_t ptr;
    mx_process_map_vm(mx_process_self(), vmo, 0, size, &ptr, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);

    t = time_it([&](){
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            __UNUSED char a = ((volatile char *)ptr)[i];
        }
    });
    printf("\ttook %" PRIu64 " nsecs to fault in vmo of size %zu (%zu pages)\n", t, size, size / PAGE_SIZE);

    t = time_it([&](){
        mx_handle_close(vmo);
    });
    printf("\ttook %" PRIu64 " nsecs to delete populated vmo of size %zu\n", t, size);

    printf("done with benchmark\n");

    return 0;
}
