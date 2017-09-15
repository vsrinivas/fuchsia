// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
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

static fbl::atomic<bool> shutdown{false};

// VM Stresser
//
// Current algorithm creates a single VMO of fairly large size, hands a handle
// to a pool of worker threads that then randomly commit/decommit/read/write/map/unmap
// the vmo asynchronously. Intended to pick out any internal races with a single VMO and
// with the VMAR mapping/unmapping system.
//
// Currently does not validate that any given operation was sucessfully performed, only
// that the apis do not return an error.
//
// Will evolve over time to use multiple VMOs simultaneously along with cloned vmos.

struct stress_thread_args {
    zx::vmo vmo;
};

static int stress_thread_entry(void* arg_ptr) {
    stress_thread_args* args = (stress_thread_args*)arg_ptr;
    zx_status_t status;

    uintptr_t ptr = 0;
    uint64_t size = 0;
    status = args->vmo.get_size(&size);
    ZX_ASSERT(size > 0);

    // allocate a local buffer
    const size_t bufsize = PAGE_SIZE * 16;
    fbl::unique_ptr<uint8_t[]> buf{new uint8_t[bufsize]};

    ZX_ASSERT(bufsize < size);

    while (!shutdown.load()) {
        int r = rand() % 100;
        switch (r) {
        case 0 ... 9: // commit a range of the vmo
            printf("c");
            status = args->vmo.op_range(ZX_VMO_OP_COMMIT, rand() % size, rand() % size, nullptr, 0);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to commit range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 10 ... 19: // decommit a range of the vmo
            printf("d");
            status = args->vmo.op_range(ZX_VMO_OP_DECOMMIT, rand() % size, rand() % size, nullptr, 0);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to decommit range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 20 ... 29:
            if (ptr) {
                // unmap the vmo if it already was
                printf("u");
                status = zx::vmar::root_self().unmap(ptr, size);
                if (status != ZX_OK) {
                    fprintf(stderr, "failed to unmap range, error %d (%s)\n", status, zx_status_get_string(status));
                }
            }
            // map it somewhere
            printf("m");
            status = zx::vmar::root_self().map(0, args->vmo, 0, size,
                                               ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to map range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 30 ... 59:
            // read from a random range of the vmo
            printf("r");
            status = args->vmo.read(buf.get(), rand() % (size - bufsize), rand() % bufsize);
            if (status != ZX_OK) {
                fprintf(stderr, "error reading from vmo\n");
            }
            break;
        case 60 ... 99:
            // write to a random range of the vmo
            printf("w");
            status = args->vmo.write(buf.get(), rand() % (size - bufsize), rand() % bufsize);
            if (status != ZX_OK) {
                fprintf(stderr, "error writing to vmo\n");
            }
            break;
        }

        fflush(stdout);
    }

    if (ptr) {
        status = zx::vmar::root_self().unmap(ptr, size);
    }
    delete args;

    return 0;
}

static int vmstress(zx_handle_t root_resource) {
    zx_info_kmem_stats_t stats;
    zx_status_t err = zx_object_get_info(
        root_resource, ZX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
    if (err != ZX_OK) {
        fprintf(stderr, "ZX_INFO_KMEM_STATS returns %d (%s)\n",
                err, zx_status_get_string(err));
        return err;
    }

    uint64_t free_bytes = stats.free_bytes;

    // scale the size of the VMO we create based on the size of memory in the system.
    // 1/64th the size of total memory generates a fairly sizeable vmo (16MB per 1GB)
    uint64_t vmo_test_size = stats.free_bytes / 64;

    printf("starting stress test: free bytes %" PRIu64 "\n", free_bytes);

    printf("creating test vmo of size %" PRIu64 "\n", vmo_test_size);

    // create a test vmo
    zx::vmo vmo;
    auto status = zx::vmo::create(vmo_test_size, 0, &vmo);
    if (status != ZX_OK)
        return status;

    // map it
    uintptr_t ptr[16];
    for (auto& p : ptr) {
        status = zx::vmar::root_self().map(0, vmo, 0, vmo_test_size,
                                           ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE, &p);
        if (status != ZX_OK) {
            fprintf(stderr, "mx_vmar_map returns %d (%s)\n", status, zx_status_get_string(status));
            return status;
        }

        memset((void*)p, 0, vmo_test_size);
    }

    // clean up all the mappings on the way out
    auto cleanup = fbl::MakeAutoCall([&] {
        for (auto& p : ptr) {
            zx::vmar::root_self().unmap(p, vmo_test_size);
        }
    });

    // create a pile of threads
    // TODO: scale based on the number of cores in the system and/or command line arg
    thrd_t thread[16];
    for (auto& t : thread) {
        stress_thread_args* args = new stress_thread_args;

        vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &args->vmo);

        thrd_create_with_name(&t, &stress_thread_entry, args, "vmstress_worker");
    }

    // sleep forever
    for (;;)
        zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

    shutdown.store(true);

    auto cleanup2 = fbl::MakeAutoCall([&] {
        for (auto& t : thread) {
            thrd_join(t, nullptr);
        }
    });

    return ZX_OK;
}

static zx_status_t get_root_resource(zx_handle_t* root_resource) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open sysinfo: %s (%d)\n",
                strerror(errno), errno);
        return ZX_ERR_NOT_FOUND;
    }

    ssize_t n = ioctl_sysinfo_get_root_resource(fd, root_resource);
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
    return ZX_OK;
}

static void print_help(char** argv, FILE* f) {
    fprintf(f, "Usage: %s [options]\n", argv[0]);
}

int main(int argc, char** argv) {
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

    zx_handle_t root_resource;
    zx_status_t ret = get_root_resource(&root_resource);
    if (ret != ZX_OK) {
        return ret;
    }

    ret = vmstress(root_resource);

    zx_handle_close(root_resource);

    return ret;
}
