// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <lib/zx/resource.h>
#include <zircon/device/sysinfo.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "stress_test.h"

namespace {

zx_status_t get_root_resource(zx::resource* root_resource) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open sysinfo: %s (%d)\n",
                strerror(errno), errno);
        return ZX_ERR_NOT_FOUND;
    }

    zx_handle_t h;
    ssize_t n = ioctl_sysinfo_get_root_resource(fd, &h);
    close(fd);

    if (n != sizeof(*root_resource)) {
        if (n < 0) {
            fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%zd)\n",
                    zx_status_get_string((zx_status_t)n), n);
            return (zx_status_t)n;
        } else {
            fprintf(stderr, "ERROR: Cannot obtain root resource (%zd != %zd)\n",
                    n, sizeof(root_resource));
            return ZX_ERR_NOT_FOUND;
        }
    }

    root_resource->reset(h);

    return ZX_OK;
}

zx_status_t get_kmem_stats(zx_info_kmem_stats_t* kmem_stats) {
    zx::resource root_resource;
    zx_status_t ret = get_root_resource(&root_resource);
    if (ret != ZX_OK) {
        return ret;
    }

    zx_status_t err = zx_object_get_info(
        root_resource.get(), ZX_INFO_KMEM_STATS, kmem_stats, sizeof(*kmem_stats), nullptr, nullptr);
    if (err != ZX_OK) {
        fprintf(stderr, "ZX_INFO_KMEM_STATS returns %d (%s)\n",
                err, zx_status_get_string(err));
        return err;
    }

    return ZX_OK;
}

void print_help(char** argv, FILE* f) {
    fprintf(f, "Usage: %s [options]\n", argv[0]);
    fprintf(f, "No options currently defined\n");
}

} // namespace

int main(int argc, char** argv) {
    zx_status_t status;

    int c;
    while ((c = getopt(argc, argv, "h")) > 0) {
        switch (c) {
        case 'h':
            print_help(argv, stderr);
            return 0;
        default:
            fprintf(stderr, "Unknown option\n");
            print_help(argv, stderr);
            return 1;
        }
    }

    // read some system stats for each test to use
    zx_info_kmem_stats_t kmem_stats;
    status = get_kmem_stats(&kmem_stats);
    if (status != ZX_OK) {
        fprintf(stderr, "error reading kmem stats\n");
        return 1;
    }

    fbl::SinglyLinkedList<fbl::unique_ptr<StressTest>> test_list;

    // create a single test and run it
    //
    // TODO: allow selecting more than one test and the timeout
    {
        auto test = CreateVmStressTest();
        if (!test) {
            fprintf(stderr, "error creating test\n");
            return 1;
        }
        test_list.push_front(fbl::move(test));
    }

    // initialize all the tests
    for (auto& test : test_list) {
        status = test.Init(kmem_stats);
        if (status != ZX_OK) {
            fprintf(stderr, "error initializing test\n");
            return 1;
        }
    }

    // start all of them
    for (auto& test : test_list) {
        status = test.Start();
        if (status != ZX_OK) {
            fprintf(stderr, "error initializing test\n");
            return 1;
        }
    }

    // run forever for now
    for (;;) {
        zx_nanosleep(zx_deadline_after(ZX_SEC(5)));
    }

    // shut them down
    for (auto& test : test_list) {
        status = test.Stop();
        if (status != ZX_OK) {
            fprintf(stderr, "error stopping test\n");
            return 1;
        }
    }

    return 0;
}
