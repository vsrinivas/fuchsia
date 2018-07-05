// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
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
    fprintf(f, "options:\n");
    fprintf(f, "\t-h:                   This help\n");
    fprintf(f, "\t-t [time in seconds]: stop all tests after the time has elapsed\n");
    fprintf(f, "\t-v:                   verbose, status output\n");
}

} // namespace

int main(int argc, char** argv) {
    zx_status_t status;

    bool verbose = false;
    zx::duration run_duration = zx::duration::infinite();

    int c;
    while ((c = getopt(argc, argv, "ht:v")) > 0) {
        switch (c) {
        case 'h':
            print_help(argv, stdout);
            return 0;
        case 't': {
            long t = atol(optarg);
            if (t <= 0) {
                fprintf(stderr, "bad time argument\n");
                print_help(argv, stderr);
                return 1;
            }
            run_duration = zx::sec(t);
            break;
        }
        case 'v':
            verbose = true;
            break;
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

    if (run_duration != zx::duration::infinite()) {
        printf("Running stress tests for %" PRIu64 " seconds\n", run_duration.to_secs());
    } else {
        printf("Running stress tests continually\n");
    }

    // initialize all the tests
    for (auto& test : StressTest::tests()) {
        printf("Initializing %s test\n", test->name());
        status = test->Init(verbose, kmem_stats);
        if (status != ZX_OK) {
            fprintf(stderr, "error initializing test\n");
            return 1;
        }
    }

    // start all of them
    for (auto& test : StressTest::tests()) {
        printf("Starting %s test\n", test->name());
        status = test->Start();
        if (status != ZX_OK) {
            fprintf(stderr, "error initializing test\n");
            return 1;
        }
    }

    // set stdin to non blocking
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    zx::time start_time = zx::clock::get_monotonic();
    bool stop = false;
    for (;;) {
        // look for ctrl-c for terminals that do not support it
        char c;
        while (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == 0x3) {
                stop = true;
                break;
            }
        }
        if (stop) {
            break;
        }

        // wait for a second to try again
        zx::nanosleep(zx::deadline_after(zx::sec(1)));

        if (run_duration != zx::duration::infinite()) {
            zx::time now = zx::clock::get_monotonic();
            if (now - start_time >= run_duration) {
                break;
            }
        }
    }

    // shut them down
    for (auto& test : StressTest::tests()) {
        printf("Stopping %s test\n", test->name());
        status = test->Stop();
        if (status != ZX_OK) {
            fprintf(stderr, "error stopping test\n");
            return 1;
        }
    }

    return 0;
}
