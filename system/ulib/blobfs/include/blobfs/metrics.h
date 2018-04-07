// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <zircon/syscalls.h>

namespace blobfs {

// Helper class for getting durations of events.
class Duration {
public:
    Duration(bool collecting_metrics) : start_(collecting_metrics ?
                                               zx_ticks_get() : 0) {}
    void reset() {
        if (start_ != 0) {
            return;
        }
        start_ = zx_ticks_get();
    }

    // Returns '0' for ns if collecting_metrics is false,
    // preventing an unnecessary syscall.
    //
    // Otherwise, returns the time in nanoseconds since either the constructor
    // or the last call to reset (whichever was more recent).
    uint64_t ns() const {
        if (start_ == 0) {
            return 0;
        }
        const zx_ticks_t end = zx_ticks_get();
        const zx_ticks_t ticks_per_nsec = zx_ticks_per_second() / ZX_SEC(1);
        return (end - start_) / ticks_per_nsec;
    }
private:
    zx_ticks_t start_;
};

struct BlobfsMetrics {
    void Dump() const {
        constexpr uint64_t mb = 1 << 20;
        printf("Allocation Info:\n");
        printf("  Allocated %zu blobs (%zu MB) in %zu ms\n", blobs_created,
               blobs_created_total_size / mb, total_allocation_time_ns / ZX_MSEC(1));
        printf("Writeback Info:\n");
        printf("  (Client) Wrote %zu MB of data and %zu MB of merkle trees\n",
               data_bytes_written / mb, merkle_bytes_written / mb);
        printf("  (Client) Enqueued writeback in %zu ms, made merkle tree in %zu ms\n",
               total_write_enqueue_time_ns / ZX_MSEC(1),
               total_merkle_generation_time_ns / ZX_MSEC(1));
        printf("  (Writeback Thread) Wrote %zu MB of data in %zu ms\n",
               total_writeback_bytes_written / mb,
               total_writeback_time_ns / ZX_MSEC(1));
        printf("Lookup Info:\n");
        printf("  Opened %zu blobs (%zu MB)\n", blobs_opened,
               blobs_opened_total_size / mb);
        printf("  Verified %zu blobs (%zu MB data, %zu MB merkle)\n",
               blobs_verified, blobs_verified_total_size_data / mb,
               blobs_verified_total_size_merkle / mb);
        printf("  Spent %zu ms reading %zu MB from disk, %zu ms verifying\n",
               total_read_from_disk_time_ns / ZX_MSEC(1),
               bytes_read_from_disk / mb,
               total_verification_time_ns / ZX_MSEC(1));
    }

    // ALLOCATION STATS

    // Created with external-facing "Create".
    uint64_t blobs_created = 0;
    // Measured by space allocated with "Truncate".
    uint64_t blobs_created_total_size = 0;
    uint64_t total_allocation_time_ns = 0;

    // WRITEBACK STATS

    // Measurements, from the client's perspective, of writing and enqueing
    // data that will later be written to disk.
    uint64_t data_bytes_written = 0;
    uint64_t merkle_bytes_written = 0;
    uint64_t total_write_enqueue_time_ns = 0;
    uint64_t total_merkle_generation_time_ns = 0;
    // Measured by true time writing back to disk. This may be distinct from
    // the client time because of asynchronous writeback buffers.
    uint64_t total_writeback_time_ns = 0;
    uint64_t total_writeback_bytes_written = 0;

    // LOOKUP STATS

    // Total time waiting for reads from disk.
    uint64_t total_read_from_disk_time_ns = 0;
    uint64_t total_read_from_disk_verify_time_ns = 0;
    uint64_t bytes_read_from_disk = 0;
    // Opened via "LookupBlob".
    uint64_t blobs_opened = 0;
    uint64_t blobs_opened_total_size = 0;
    // Verified blob data (includes both blobs read and written).
    uint64_t blobs_verified = 0;
    uint64_t blobs_verified_total_size_data = 0;
    uint64_t blobs_verified_total_size_merkle = 0;
    uint64_t total_verification_time_ns = 0;

    // FVM STATS
    // TODO(smklein)
};

} // namespace blobfs
