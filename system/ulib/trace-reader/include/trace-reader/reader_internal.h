// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <trace-engine/buffer_internal.h>
#include <trace-reader/reader.h>
#include <zircon/assert.h>

namespace trace {
namespace internal {

// Trace buffers have a header and one to three sub-buffers.
// This class provides an API for interpreting the header.
// See trace-engine/buffer.h for details.
class BufferHeaderReader {
public:
    // Create a reader for the header at |header|.
    // The memory object containing |header| must survive this object.
    // |buffer_size| is the size of the full trace buffer, and is used to
    // validate the contents of the header.
    // Returns "" on success or an error message.
    // |header| must be suitably aligned to point to a header.
    static fbl::String Create(
        const void* header, size_t buffer_size,
        fbl::unique_ptr<BufferHeaderReader>* out_reader);

    static int GetBufferNumber(uint32_t wrapped_count) {
        static_assert(fbl::count_of(
                          static_cast<trace_buffer_header*>(
                              nullptr)->nondurable_data_end) == 2, "");
        return wrapped_count & 1;
    }

    trace_buffering_mode_t buffering_mode() const {
        return static_cast<trace_buffering_mode_t>(header_->buffering_mode);
    }

    uint32_t wrapped_count() const { return header_->wrapped_count; }

    uint64_t total_size() const { return header_->total_size; }

    uint64_t durable_buffer_size() const {
        return header_->durable_buffer_size;
    }

    uint64_t nondurable_buffer_size() const {
        return header_->nondurable_buffer_size;
    }

    uint64_t durable_data_end() const { return header_->durable_data_end; }

    uint64_t nondurable_data_end(int buffer_number) const {
        //static_assert(fbl::count_of(header_->nondurable_data_end) == 2, "");
        ZX_DEBUG_ASSERT(buffer_number >= 0 && buffer_number <= 1);
        return header_->nondurable_data_end[buffer_number];
    }

    uint64_t num_records_dropped() const {
        return header_->num_records_dropped;
    }

    // Return the offset of the durable buffer.
    uint64_t get_durable_buffer_offset() const {
        return sizeof(trace_buffer_header);
    }

    // Given a pointer to a trace buffer, return a pointer to the durable
    // buffer contained therein.
    const void* GetDurableBuffer(const void* buffer) const {
        auto buf = reinterpret_cast<const uint8_t*>(buffer);
        return buf + get_durable_buffer_offset();
    }

    // Return the offset of nondurable buffer |buffer_number|.
    uint64_t GetNondurableBufferOffset(int buffer_number) const {
        //static_assert(fbl::count_of(header_->nondurable_data_end) == 2, "");
        ZX_DEBUG_ASSERT(buffer_number >= 0 && buffer_number <= 1);
        auto offset = sizeof(trace_buffer_header) + durable_buffer_size();
        if (buffer_number == 1) {
            offset += nondurable_buffer_size();
        }
        return offset;
    }

    // Given a pointer to a trace buffer and a nondurable buffer number,
    // return a pointer to the nondurable buffer contained therein.
    const void* GetNondurableBuffer(const void* buffer, int buffer_number) const {
        auto buf = reinterpret_cast<const uint8_t*>(buffer);
        return buf + GetNondurableBufferOffset(buffer_number);
    }

private:
    explicit BufferHeaderReader(const trace_buffer_header* header);

    static fbl::String Validate(const trace_buffer_header& header,
                                size_t buffer_size);

    const trace_buffer_header* const header_;

    BufferHeaderReader(const BufferHeaderReader&) = delete;
    BufferHeaderReader(BufferHeaderReader&&) = delete;
    BufferHeaderReader& operator=(const BufferHeaderReader&) = delete;
    BufferHeaderReader& operator=(BufferHeaderReader&&) = delete;
};

// Reads a trace buffer a chunk at a time, where the buffer has a trace
// buffer header and subsequent contents.
// |chunk_consumer| is invoked for each chunk in the buffer.
class TraceBufferReader {
public:
    // Called once for each chunk read by |ReadChunks|.
    using ChunkConsumer = fbl::Function<void(Chunk)>;

    // Callback invoked when an error is detected.
    using ErrorHandler = fbl::Function<void(fbl::String)>;

    TraceBufferReader(ChunkConsumer chunk_consumer,
                      ErrorHandler error_handler);
    ~TraceBufferReader() = default;

    // Reads as many chunks as possible from the buffer, invoking the chunk
    // consumer for each (non-empty) one.
    // |buffer| must be suitably aligned to point to a trace buffer header.
    // Returns true on success, false if the buffer header is malformed.
    bool ReadChunks(const void* buffer, size_t buffer_size);

private:
    void CallChunkConsumerIfNonEmpty(const void* chunk, size_t size);

    fbl::unique_ptr<BufferHeaderReader> const header_;
    ChunkConsumer chunk_consumer_;
    ErrorHandler error_handler_;

    TraceBufferReader(const TraceBufferReader&) = delete;
    TraceBufferReader(TraceBufferReader&&) = delete;
    TraceBufferReader& operator=(const TraceBufferReader&) = delete;
    TraceBufferReader& operator=(TraceBufferReader&&) = delete;
};

} // namespace internal
} // namespace trace
