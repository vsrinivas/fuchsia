// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>

#include <fbl/atomic.h>

#include <trace-engine/buffer_internal.h>
#include <trace-engine/context.h>
#include <trace-engine/handler.h>

using trace::internal::trace_buffer_header;

// Maintains state for a single trace session.
// This structure is accessed concurrently from many threads which hold trace
// context references.
// Implements the opaque type declared in <trace-engine/context.h>.
struct trace_context {
    trace_context(void* buffer, size_t buffer_num_bytes, trace_buffering_mode_t buffering_mode,
                  trace_handler_t* handler);

    ~trace_context();

    const trace_buffer_header* buffer_header() const { return header_; }

    static size_t min_buffer_size() { return kMinPhysicalBufferSize; }

    static size_t max_buffer_size() { return kMaxPhysicalBufferSize; }

    static size_t usable_buffer_end() {
        return 1ull << kUsableBufferOffsetBits;
    }

    uint32_t generation() const { return generation_; }

    trace_handler_t* handler() const { return handler_; }

    trace_buffering_mode_t buffering_mode() const { return buffering_mode_; }

    uint64_t num_records_dropped() const {
        return num_records_dropped_.load(fbl::memory_order_relaxed);
    }

    // Return true if at least one record was dropped.
    bool record_dropped() const { return num_records_dropped() != 0u; }

    // Return the number of bytes currently allocated in the buffer.
    // This does not include the durable buffer.
    // TODO(dje): Renaming this to nondurable_bytes_allocated() is ugly,
    // which suggests removing all nondurable_ prefixes.
    size_t bytes_allocated() const;

    void InitBufferHeader();
    void UpdateBufferHeaderAfterStopped();

    uint64_t* AllocRecord(size_t num_bytes);
    bool AllocThreadIndex(trace_thread_index_t* out_index);
    bool AllocStringIndex(trace_string_index_t* out_index);

private:
    // The maximum nondurable buffer size in bits.
    static constexpr size_t kNondurableBufferSizeBits = 32;

    // Maximum size, in bytes, of a nondurable buffer.
    static constexpr size_t kMaxNondurableBufferSize = 1ull << kNondurableBufferSizeBits;

    // The number of usable bits in the buffer pointer.
    // This is several bits more than the maximum buffer size to allow a
    // buffer pointer to grow without overflow while TraceManager is saving a
    // buffer in streaming mode.
    // In this case we don't snap the offset to the end as doing so requires
    // modifying state and thus obtaining the lock (streaming mode is not
    // lock-free). Instead the offset keeps growing.
    // kUsableBufferOffsetBits = 40 bits = 1TB.
    // Max nondurable buffer size = 32 bits = 4GB.
    // Thus we assume TraceManager can save 4GB of trace before the client
    // writes 1TB of trace data (lest the offset part of
    // |nondurable_buffer_current_| overflows). But, just in case, if
    // TraceManager still can't keep up we stop tracing when the offset
    // approaches overflowing. See AllocRecord().
    static constexpr int kUsableBufferOffsetBits = kNondurableBufferSizeBits + 8;

    // The number of bits used to record the buffer pointer.
    // This includes one more bit to support overflow in offset calcs.
    static constexpr int kBufferOffsetBits = kUsableBufferOffsetBits + 1;

    // The number of bits in the wrapped counter.
    // It important that this counter not wrap (well, technically it can,
    // the lost information isn't that important, but if it wraps too
    // quickly the transition from one buffer to the other can break.
    // The current values allow for a 20 bit counter which is plenty.
    // A value of 20 also has the benefit that when the entire
    // offset_plus_counter value is printed in hex the counter is easily read.
    static constexpr int kWrappedCounterBits = 20;
    static constexpr int kWrappedCounterShift = 64 - kWrappedCounterBits;

    static_assert(kBufferOffsetBits + kWrappedCounterBits <= 64, "");

    // The physical buffer must be at least this big.
    // Mostly this is here to simplify buffer size calculations.
    // It's as small as it is to simplify some testcases.
    static constexpr size_t kMinPhysicalBufferSize = 4096;

    // The physical buffer can be at most this big.
    // To keep things simple we ignore the header.
    static constexpr size_t kMaxPhysicalBufferSize = kMaxNondurableBufferSize;

    static uintptr_t GetBufferOffset(uint64_t offset_plus_counter) {
        return offset_plus_counter & ((1ul << kBufferOffsetBits) - 1);
    }

    static uint32_t GetWrappedCount(uint64_t offset_plus_counter) {
        return static_cast<uint32_t>(offset_plus_counter >> kWrappedCounterShift);
    }

    static uint64_t MakeOffsetPlusCounter(uintptr_t offset, uint32_t counter) {
        return offset | (static_cast<uint64_t>(counter) << kWrappedCounterShift);
    }

    static int GetBufferNumber(uint32_t wrapped_count) {
        return wrapped_count & 1;
    }

    void ComputeBufferSizes();

    void MarkOneshotBufferFull(uint64_t last_offset);

    void SnapToEnd(uint32_t wrapped_count) {
        // Snap to the endpoint for simplicity.
        // Several threads could all hit buffer-full with each one
        // continually incrementing the offset.
        uint64_t full_offset_plus_counter =
            MakeOffsetPlusCounter(nondurable_buffer_size_, wrapped_count);
        nondurable_buffer_current_.store(full_offset_plus_counter,
                                         fbl::memory_order_relaxed);
    }

    void MarkRecordDropped() {
        num_records_dropped_.fetch_add(1, fbl::memory_order_relaxed);
    }

    // The generation counter associated with this context to distinguish
    // it from previously created contexts.
    uint32_t const generation_;

    // The buffering mode.
    trace_buffering_mode_t const buffering_mode_;

    // Buffer start and end pointers.
    // These encapsulate the entire physical buffer.
    uint8_t* const buffer_start_;
    uint8_t* const buffer_end_;

    // Same as |buffer_start_|, but as a header pointer.
    trace_buffer_header* const header_;

    // Durable-record buffer start.
    uint8_t* durable_buffer_start_;

    // The size of the durable buffer;
    size_t durable_buffer_size_;

    // Non-durable record buffer start.
    // To simplify switching between them we don't record the buffer end,
    // and instead record their size (which is identical).
    uint8_t* nondurable_buffer_start_[2];

    // The size of both non-durable buffers.
    size_t nondurable_buffer_size_;

    // Current allocation pointer for durable records.
    // This only used in circular and streaming modes.
    // Starts at |durable_buffer_start| and grows from there.
    // May exceed |durable_buffer_end| when the buffer is full.
    fbl::atomic<uint64_t> durable_buffer_current_;

    // Offset beyond the last successful allocation, or zero if not full.
    // This only used in circular and streaming modes: There is no separate
    // buffer for durable records in oneshot mode.
    // Only ever set to non-zero once in the lifetime of the trace context.
    fbl::atomic<uint64_t> durable_buffer_full_mark_;

    // Allocation pointer of the current buffer for non-durable records,
    // plus a wrapped counter. These are combined into one so that they can
    // be atomically fetched together.
    // The lower |kBufferOffsetBits| bits comprise the offset into the buffer
    // of the next record to write. The upper |kWrappedCountBits| comprise
    // the wrapped counter. Bit zero of this counter is the number of the
    // buffer currently being written to. The counter is used in part for
    // record keeping purposes, and to support transition from one buffer to
    // the next.
    //
    // To construct: make_offset_plus_counter
    // To get buffer offset: get_buffer_offset
    // To get wrapped count: get_wrapped_count
    //
    // This value is also used for durable records in oneshot mode: in
    // oneshot mode durable and non-durable records share the same buffer.
    fbl::atomic<uint64_t> nondurable_buffer_current_;

    // Offset beyond the last successful allocation, or zero if not full.
    // Only ever set to non-zero once when the buffer fills.
    // This will only be set in oneshot and streaming modes.
    fbl::atomic<uint64_t> nondurable_buffer_full_mark_[2];

    // A count of the number of records that have been dropped.
    fbl::atomic<uint64_t> num_records_dropped_{0};

    // Handler associated with the trace session.
    trace_handler_t* const handler_;

    // The next thread index to be assigned.
    fbl::atomic<trace_thread_index_t> next_thread_index_{
        TRACE_ENCODED_THREAD_REF_MIN_INDEX};

    // The next string table index to be assigned.
    fbl::atomic<trace_string_index_t> next_string_index_{
        TRACE_ENCODED_STRING_REF_MIN_INDEX};
};
