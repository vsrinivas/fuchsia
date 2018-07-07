// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Buffer layout.
// This is an internal header between trace-engine and trace-provider.
// It may also be used by various tests.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <trace-engine/context.h>

namespace trace {
namespace internal {

// This header provides framing information about the buffer, for use in
// implementing circular buffering and double(streaming) buffering.
//
// Writing to the buffer has conceptually three modes:
// oneshot, circular, streaming.
//
// Buffers are passed from Trace Manager to Trace Provider in vmos.
// How the buffer is treated depends on the writing mode.
// For "oneshot" mode the vmo is one big simple buffer.
//   Using one big buffer means durable and non-durable records all share the
//   same buffer.
// For "circular" and "streaming" buffering modes, the vmo is treated as a
// "virtual buffer" and is split into three logical parts:
//   - one buffer for "durable" records
//   - two buffers, labeled 0 and 1, for "non-durable" records
// Writing switches back and forth between the two non-durable buffers as each
// fills. Streaming buffering differs from circular buffering in that the Trace
// Manager is involved in saving each non-durable buffer as it fills.
// Besides consistency, a nice property of using two separate buffers for
// circular mode is that, because records are variable sized, there are no
// issues trying to find the "first" non-durable record in the complete virtual
// buffer after a wrap: It's always the first record of the other non-durable
// buffer.
//
// To help preserve data integrity tracing stops when the durable buffer fills,
// even in circular mode.
// TODO(dje): Relax this restriction, and accept potentially more lost data.
//
// Durable records:
// - initialization record
// - string table
// - thread table
// TODO(dje): Move initialization record to header?
//
// Non-durable records:
// - everything else
//
// The total physical buffer is laid out as follows (without gaps):
// - header
// - durable buffer (empty in oneshot mode)
// - non-durable buffer 0
// - non-durable buffer 1 (empty in oneshot mode)
//
// It is an invariant that:
// oneshot:
//   total_size == header + nondurable_buffer_size
// circular/streaming:
//   total_size == header + durable_buffer_size + 2 * nondurable_buffer_size
//
// All buffer sizes must be a multiple of 8 as all records are a multiple of 8.

struct trace_buffer_header {
    // Standard magic number field.
    uint64_t magic;
#define TRACE_BUFFER_HEADER_MAGIC ((uint64_t) 0x627566ee68656164ull)

    uint16_t version;
#define TRACE_BUFFER_HEADER_V0 ((uint16_t) 0)

    // One of |trace_buffering_mode_t|.
    uint8_t buffering_mode;

    // For alignment and future concerns.
    uint8_t reserved1;

    // A count of the number of times writing wrapped.
    // If zero then writing didn't wrap. If non-zero then |wrapped_count % 2|
    // is the buffer number where writing finished.
    uint32_t wrapped_count;

    // The size of the buffer in bytes, including this header.
    // In other words this is the size of the vmo.
    uint64_t total_size;

    // The size in bytes of the durable record buffer.
    // This is zero in oneshot mode.
    uint64_t durable_buffer_size;

    // The size in bytes of each of the non-durable record buffers.
    uint64_t nondurable_buffer_size;

    // The offset, from the first data byte, to the end of recorded durable
    // data. This starts at zero and is not written to while writing the buffer
    // is active. This remains zero in oneshot mode (since there is no separate
    // buffer for durable records). It is written to when the buffer fills or
    // when tracing is stopped.
    uint64_t durable_data_end;

    // The offset, from the first data byte, to the end of recorded data.
    // In oneshot mode only [0] is used. This starts at zero and is not written
    // to while writing the buffer is active. It is written to when the buffer
    // fills or when tracing is stopped.
    uint64_t nondurable_data_end[2];

    // Total number of records dropped thus far.
    uint64_t num_records_dropped;

    // The header is padded out to a size of 128 to provide room for growth,
    // and to simplify internal buffer size calcs.
    // The remainder of the header is reserved.
    uint64_t reserved[7];
};

static_assert(sizeof(trace_buffer_header) == 128, "");

} // namespace internal
} // namespace trace

// Update the buffer header and snapshot a copy of it.
// This is only intended to be used for testing purposes.
// Testcases needing the definition need to do:
// #include <trace-engine/buffer.h>
// using trace::internal::trace_buffer_header;
//
// This function is not thread-safe relative to the collected data, and
// assumes tracing is stopped or at least paused.
void trace_context_snapshot_buffer_header(
    trace_context_t* context,
    ::trace::internal::trace_buffer_header* header);
