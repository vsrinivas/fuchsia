// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>

#include <fbl/atomic.h>

#include <trace-engine/context.h>
#include <trace-engine/handler.h>

// Maintains state for a single trace session.
// This structure is accessed concurrently from many threads which hold trace
// context references.
// Implements the opaque type declared in <trace-engine/context.h>.
struct trace_context {
    trace_context(void* buffer, size_t buffer_num_bytes, trace_handler_t* handler);

    ~trace_context();

    uint32_t generation() const { return generation_; }

    trace_handler_t* handler() const { return handler_; }

    bool is_buffer_full() const {
        return buffer_full_mark_.load(fbl::memory_order_relaxed) != 0u;
    }

    size_t bytes_allocated() const {
        uintptr_t tail = buffer_full_mark_.load(fbl::memory_order_relaxed);
        if (!tail)
            tail = buffer_current_.load(fbl::memory_order_relaxed);
        return reinterpret_cast<uint8_t*>(tail) - buffer_start_;
    }

    uint64_t* AllocRecord(size_t num_bytes);
    bool AllocThreadIndex(trace_thread_index_t* out_index);
    bool AllocStringIndex(trace_string_index_t* out_index);

private:
    // The generation counter associated with this context to distinguish
    // it from previously created contexts.
    uint32_t const generation_;

    // Buffer start and end pointers.
    uint8_t* const buffer_start_;
    uint8_t* const buffer_end_;

    // Current allocation pointer.
    // Starts at |buffer_start| and grows from there.
    // May exceed |buffer_end| when the buffer is full.
    fbl::atomic<uintptr_t> buffer_current_;

    // Pointer beyond the last successful allocation, or null if not full.
    // Only ever set to non-null once in the lifetime of the trace context.
    fbl::atomic<uintptr_t> buffer_full_mark_;

    // Handler associated with the trace session.
    trace_handler_t* const handler_;

    // The next thread index to be assigned.
    fbl::atomic<trace_thread_index_t> next_thread_index_{
        TRACE_ENCODED_THREAD_REF_MIN_INDEX};

    // The next string table index to be assigned.
    fbl::atomic<trace_string_index_t> next_string_index_{
        TRACE_ENCODED_STRING_REF_MIN_INDEX};
};
