// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains information for gathering Blobfs metrics.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/time.h>

namespace blobfs {

struct BlobfsMetrics {
    // Print information about metrics to stdout.
    //
    // TODO(ZX-1999): This is a stop-gap solution; long-term, this information
    // should be extracted from devices.
    void Dump() const;

    // ALLOCATION STATS

    // Created with external-facing "Create".
    uint64_t blobs_created = 0;
    // Measured by space allocated with "Truncate".
    uint64_t blobs_created_total_size = 0;
    zx::ticks total_allocation_time_ticks = {};

    // WRITEBACK STATS

    // Measurements, from the client's perspective, of writing and enqueing
    // data that will later be written to disk.
    uint64_t data_bytes_written = 0;
    uint64_t merkle_bytes_written = 0;
    zx::ticks total_write_enqueue_time_ticks = {};
    zx::ticks total_merkle_generation_time_ticks = {};
    // Measured by true time writing back to disk. This may be distinct from
    // the client time because of asynchronous writeback buffers.
    zx::ticks total_writeback_time_ticks = {};
    uint64_t total_writeback_bytes_written = 0;

    // LOOKUP STATS

    // Total time waiting for reads from disk.
    zx::ticks total_read_from_disk_time_ticks = {};
    zx::ticks total_read_from_disk_verify_time_ticks = {};
    uint64_t bytes_read_from_disk = 0;
    // Opened via "LookupBlob".
    uint64_t blobs_opened = 0;
    uint64_t blobs_opened_total_size = 0;
    // Verified blob data (includes both blobs read and written).
    uint64_t blobs_verified = 0;
    uint64_t blobs_verified_total_size_data = 0;
    uint64_t blobs_verified_total_size_merkle = 0;
    zx::ticks total_verification_time_ticks = {};

    // FVM STATS
    // TODO(smklein)
};

} // namespace blobfs
