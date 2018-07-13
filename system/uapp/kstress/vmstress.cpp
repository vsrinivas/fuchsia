// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/thread.h>
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

#include "stress_test.h"

class VmStressTest : public StressTest {
public:
    VmStressTest() = default;
    virtual ~VmStressTest() = default;

    virtual zx_status_t Start();
    virtual zx_status_t Stop();

    virtual const char* name() const { return "VM Stress"; }

private:
    int stress_thread();

    thrd_t threads_[16]{};

    // used by the worker threads at runtime
    fbl::atomic<bool> shutdown_{false};
    zx::vmo vmo_{};
};

// our singleton
VmStressTest vmstress;

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

int VmStressTest::stress_thread() {
    zx_status_t status;

    uintptr_t ptr = 0;
    uint64_t vmo_size = 0;
    status = vmo_.get_size(&vmo_size);
    ZX_ASSERT(vmo_size > 0);

    // allocate a local buffer
    const size_t buf_size = PAGE_SIZE * 16;
    fbl::unique_ptr<uint8_t[]> buf{new uint8_t[buf_size]};

    // local helper routines to calculate a random range within a vmo and
    // a range appropriate to read into the local buffer above
    auto rand_vmo_range = [vmo_size](uint64_t *out_offset, uint64_t *out_size) {
        *out_offset = rand() % vmo_size;
        *out_size = fbl::min(rand() % vmo_size, vmo_size - *out_offset);
    };
    auto rand_buffer_range = [vmo_size](uint64_t *out_offset, uint64_t *out_size) {
        *out_size = rand() % buf_size;
        *out_offset = rand() % (vmo_size - *out_size);
    };

    ZX_ASSERT(buf_size < vmo_size);

    while (!shutdown_.load()) {
        uint64_t off, len;

        int r = rand() % 100;
        switch (r) {
        case 0 ... 9: // commit a range of the vmo
            Printf("c");
            rand_vmo_range(&off, &len);
            status = vmo_.op_range(ZX_VMO_OP_COMMIT, off, len, nullptr, 0);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to commit range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 10 ... 19: // decommit a range of the vmo
            Printf("d");
            rand_vmo_range(&off, &len);
            status = vmo_.op_range(ZX_VMO_OP_DECOMMIT, off, len, nullptr, 0);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to decommit range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 20 ... 29:
            if (ptr) {
                // unmap the vmo if it already was
                Printf("u");
                status = zx::vmar::root_self()->unmap(ptr, vmo_size);
                if (status != ZX_OK) {
                    fprintf(stderr, "failed to unmap range, error %d (%s)\n", status, zx_status_get_string(status));
                }
                ptr = 0;
            }
            // map it somewhere
            Printf("m");
            status = zx::vmar::root_self()->map(0, vmo_, 0, vmo_size,
                                               ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);
            if (status != ZX_OK) {
                fprintf(stderr, "failed to map range, error %d (%s)\n", status, zx_status_get_string(status));
            }
            break;
        case 30 ... 39:
            // read from a random range of the vmo
            Printf("r");
            rand_buffer_range(&off, &len);
            status = vmo_.read(buf.get(), off, len);
            if (status != ZX_OK) {
                fprintf(stderr, "error reading from vmo\n");
            }
            break;
        case 40 ... 49:
            // write to a random range of the vmo
            Printf("w");
            rand_buffer_range(&off, &len);
            status = vmo_.write(buf.get(), off, len);
            if (status != ZX_OK) {
                fprintf(stderr, "error writing to vmo\n");
            }
            break;
        case 50 ... 74:
            // read from a random range of the vmo via a direct memory reference
            if (ptr) {
                Printf("R");
                rand_buffer_range(&off, &len);
                memcpy(buf.get(), reinterpret_cast<const void *>(ptr + off), len);
            }
            break;
        case 75 ... 99:
            // write to a random range of the vmo via a direct memory reference
            if (ptr) {
                Printf("W");
                rand_buffer_range(&off, &len);
                memcpy(reinterpret_cast<void *>(ptr + off), buf.get(), len);
            }
            break;
        }

        fflush(stdout);
    }

    if (ptr) {
        status = zx::vmar::root_self()->unmap(ptr, vmo_size);
    }

    return 0;
}

zx_status_t VmStressTest::Start() {
    const uint64_t free_bytes = kmem_stats_.free_bytes;

    // scale the size of the VMO we create based on the size of memory in the system.
    // 1/64th the size of total memory generates a fairly sizeable vmo (16MB per 1GB)
    const uint64_t vmo_test_size = free_bytes / 64;

    PrintfAlways("VM stress test: using vmo of size %" PRIu64 "\n", vmo_test_size);

    // create a test vmo
    auto status = zx::vmo::create(vmo_test_size, 0, &vmo_);
    if (status != ZX_OK)
        return status;

    // create a pile of threads
    // TODO: scale based on the number of cores in the system and/or command line arg
    auto worker = [](void* arg) -> int {
        VmStressTest* test = static_cast<VmStressTest*>(arg);

        return test->stress_thread();
    };

    for (auto& t : threads_) {
        thrd_create_with_name(&t, worker, this, "vmstress_worker");
    }

    return ZX_OK;
}

zx_status_t VmStressTest::Stop() {
    shutdown_.store(true);

    for (auto& t : threads_) {
        thrd_join(t, nullptr);
    }

    return ZX_OK;
}
