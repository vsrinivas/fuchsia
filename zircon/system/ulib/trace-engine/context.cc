// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes on buffering modes
// ------------------------
//
// Threads and strings are cached to improve performance and reduce buffer
// usage. The caching involves emitting separate records that identify
// threads/strings and then referring to them by a numeric id. For performance
// each thread in the application maintains its own cache.
//
// Oneshot: The trace buffer is just one large buffer, and records are written
// until the buffer is full after which all further records are dropped.
//
// Circular:
// The trace buffer is effectively divided into two pieces, and tracing begins
// by writing to the first piece. Once one buffer fills we start writing to the
// other one. This results in half the buffer being dropped at every switch,
// but simplifies things because we don't have to worry about varying record
// lengths.
//
// Streaming:
// The trace buffer is effectively divided into two pieces, and tracing begins
// by writing to the first piece. Once one buffer fills we start writing to the
// other buffer, if it is available, and notify the handler that the buffer is
// full. If the other buffer is not available, then records are dropped until
// it becomes available. The other buffer is unavailable between the point when
// it filled and when the handler reports back that the buffer's contents have
// been saved.
//
// There are two important properties we wish to preserve in circular and
// streaming modes:
// 1) We don't want records describing threads and strings to be dropped:
// otherwise records referring to them will have nothing to refer to.
// 2) We don't want thread records to be dropped at all: Fidelity of recording
// of all traced threads is important, even if some of their records are
// dropped.
// To implement both (1) and (2) we introduce a third buffer that holds
// records we don't want to drop called the "durable buffer". Threads and
// small strings are recorded there. The two buffers holding normal trace
// output are called "rolling buffers", as they fill we roll from one to the
// next. Thread and string records typically aren't very large, the durable
// buffer can hold a lot of records. To keep things simple, until there's a
// compelling reason to do something more, once the durable buffer fills
// tracing effectively stops, and all further records are dropped.
// Note: The term "rolling buffer" is intended to be internal to the trace
// engine/reader/manager and is not intended to appear in public APIs
// (at least not today). Externally, the two rolling buffers comprise the
// "nondurable" buffer.
//
// The protocol between the trace engine and the handler for saving buffers in
// streaming mode is as follows:
// 1) Buffer fills -> handler gets notified via
//    |trace_handler_ops::notify_buffer_full()|. Two arguments are passed
//    along with this request:
//    |wrapped_count| - records how many times tracing has wrapped from one
//    buffer to the next, and also records the current buffer which is the one
//    needing saving. Since there are two rolling buffers, the buffer to save
//    is |wrapped_count & 1|.
//    |durable_data_end| - records how much data has been written to the
//    durable buffer thus far. This data needs to be written before data from
//    the rolling buffers is written so string and thread references work.
// 2) The handler receives the "notify_buffer_full" request.
// 3) The handler saves new durable data since the last time, saves the
//    rolling buffer, and replies back to the engine via
//    |trace_engine_mark_buffer_saved()|.
// 4) The engine receives this notification and marks the buffer as now empty.
//    The next time the engine tries to allocate space from this buffer it will
//    succeed.
// Note that the handler is free to save buffers at whatever rate it can
// manage. The protocol allows for records to be dropped if buffers can't be
// saved fast enough.

#include "context_impl.h"

#include <assert.h>
#include <inttypes.h>

#include <lib/trace-engine/fields.h>
#include <lib/trace-engine/handler.h>

#include <atomic>
#include <mutex>

namespace trace {
namespace {

// The next context generation number.
std::atomic<uint32_t> g_next_generation{1u};

} // namespace
} // namespace trace

trace_context::trace_context(void* buffer, size_t buffer_num_bytes,
                             trace_buffering_mode_t buffering_mode,
                             trace_handler_t* handler)
    : generation_(trace::g_next_generation.fetch_add(1u, std::memory_order_relaxed) + 1u),
      buffering_mode_(buffering_mode),
      buffer_start_(reinterpret_cast<uint8_t*>(buffer)),
      buffer_end_(buffer_start_ + buffer_num_bytes),
      header_(reinterpret_cast<trace_buffer_header*>(buffer)),
      handler_(handler) {
    ZX_DEBUG_ASSERT(buffer_num_bytes >= kMinPhysicalBufferSize);
    ZX_DEBUG_ASSERT(buffer_num_bytes <= kMaxPhysicalBufferSize);
    ZX_DEBUG_ASSERT(generation_ != 0u);
    ComputeBufferSizes();
    ResetBufferPointers();
}

trace_context::~trace_context() = default;

uint64_t* trace_context::AllocRecord(size_t num_bytes) {
    ZX_DEBUG_ASSERT((num_bytes & 7) == 0);
    if (unlikely(num_bytes > TRACE_ENCODED_RECORD_MAX_LENGTH))
        return nullptr;
    static_assert(TRACE_ENCODED_RECORD_MAX_LENGTH < kMaxRollingBufferSize, "");

    // For the circular and streaming cases, try at most once for each buffer.
    // Note: Keep the normal case of one successful pass the fast path.
    // E.g., We don't do a mode comparison unless we have to.

    for (int iter = 0; iter < 2; ++iter) {
        // TODO(dje): This can be optimized a bit. Later.
        uint64_t offset_plus_counter =
            rolling_buffer_current_.fetch_add(num_bytes,
                                              std::memory_order_relaxed);
        uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
        int buffer_number = GetBufferNumber(wrapped_count);
        uint64_t buffer_offset = GetBufferOffset(offset_plus_counter);
        // Note: There's no worry of an overflow in the calcs here.
        if (likely(buffer_offset + num_bytes <= rolling_buffer_size_)) {
            uint8_t* ptr = rolling_buffer_start_[buffer_number] + buffer_offset;
            return reinterpret_cast<uint64_t*>(ptr); // success!
        }

        // Buffer is full!

        switch (buffering_mode_) {
        case TRACE_BUFFERING_MODE_ONESHOT:
            ZX_DEBUG_ASSERT(iter == 0);
            ZX_DEBUG_ASSERT(wrapped_count == 0);
            ZX_DEBUG_ASSERT(buffer_number == 0);
            MarkOneshotBufferFull(buffer_offset);
            return nullptr;
        case TRACE_BUFFERING_MODE_STREAMING: {
            MarkRollingBufferFull(wrapped_count, buffer_offset);
            // If the TraceManager is slow in saving buffers we could get
            // here a lot. Do a quick check and early exit for this case.
            if (unlikely(!IsOtherRollingBufferReady(buffer_number))) {
                MarkRecordDropped();
                StreamingBufferFullCheck(wrapped_count, buffer_offset);
                return nullptr;
            }
            break;
        }
        case TRACE_BUFFERING_MODE_CIRCULAR:
            MarkRollingBufferFull(wrapped_count, buffer_offset);
            break;
        default:
            __UNREACHABLE;
        }

        if (iter == 1) {
            // Second time through. We tried one buffer, it was full.
            // We then switched to the other buffer, which was empty at
            // the time, and now it is full too. This is technically
            // possible in either circular or streaming modes, but rare.
            // There are two possibilities here:
            // 1) Keep trying (gated by some means).
            // 2) Drop the record.
            // In order to not introduce excessive latency into the app
            // we choose (2). To assist the developer we at least provide
            // a record that this happened, but since it's rare we keep
            // it simple and maintain just a global count and no time
            // information.
            num_records_dropped_after_buffer_switch_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }

        if (!SwitchRollingBuffer(wrapped_count, buffer_offset)) {
            MarkRecordDropped();
            return nullptr;
        }

        // Loop and try again.
    }

    __UNREACHABLE;
}

void trace_context::StreamingBufferFullCheck(uint32_t wrapped_count,
                                             uint64_t buffer_offset) {
    // We allow the current offset to grow and grow as each
    // new tracing request is made: It's a trade-off to not penalize
    // performance in this case. The number of counter bits is enough
    // to not make this a concern: See the comments for
    // |kUsableBufferOffsetBits|.
    //
    // As an absolute paranoia check, if the current buffer offset
    // approaches overflow, grab the lock and snap the offset back
    // to the end of the buffer. We grab the lock so that the
    // buffer can't change while we're doing this.
    if (unlikely(buffer_offset > MaxUsableBufferOffset())) {
        std::lock_guard<std::mutex> lock(buffer_switch_mutex_);
        uint32_t current_wrapped_count = CurrentWrappedCount();
        if (GetBufferNumber(current_wrapped_count) ==
            GetBufferNumber(wrapped_count)) {
            SnapToEnd(wrapped_count);
        }
    }
}

// Returns false if there's some reason to not record this record.

bool trace_context::SwitchRollingBuffer(uint32_t wrapped_count,
                                        uint64_t buffer_offset) {
    // While atomic variables are used to track things, we switch
    // buffers under the lock due to multiple pieces of state being
    // changed.
    std::lock_guard<std::mutex> lock(buffer_switch_mutex_);

    // If the durable buffer happened to fill while we were waiting for
    // the lock we're done.
    if (unlikely(tracing_artificially_stopped_)) {
        return false;
    }

    uint32_t current_wrapped_count = CurrentWrappedCount();
    // Anything allocated to the durable buffer after this point
    // won't be for this buffer. This is racy, but all we need is
    // some usable value for where the durable pointer is.
    uint64_t durable_data_end = DurableBytesAllocated();

    ZX_DEBUG_ASSERT(wrapped_count <= current_wrapped_count);
    if (likely(wrapped_count == current_wrapped_count)) {
        // Haven't switched buffers yet.
        if (buffering_mode_ == TRACE_BUFFERING_MODE_STREAMING) {
            // Is the other buffer ready?
            if (!IsOtherRollingBufferReady(GetBufferNumber(wrapped_count))) {
                // Nope. There are two possibilities here:
                // 1) Wait for the other buffer to be saved.
                // 2) Start dropping records until the other buffer is
                //    saved.
                // In order to not introduce excessive latency into the
                // app we choose (2). To assist the developer we at
                // least provide a record that indicates the window
                // during which we dropped records.
                // TODO(dje): Maybe have a future mode where we block
                // until there's space. This is useful during some
                // kinds of debugging: Something is going wrong and we
                // care less about performance and more about keeping
                // data, and the dropped data may be the clue to find
                // the bug.
                return false;
            }

            SwitchRollingBufferLocked(wrapped_count, buffer_offset);

            // Notify the handler so it starts saving the buffer if
            // we're in streaming mode.
            // Note: The actual notification must be done *after*
            // updating the buffer header: we need trace_manager to
            // see the updates. The handler will get notified on the
            // engine's async loop (and thus can't call back into us
            // while we still have the lock).
            NotifyRollingBufferFullLocked(wrapped_count, durable_data_end);
        } else {
            SwitchRollingBufferLocked(wrapped_count, buffer_offset);
        }
    } else {
        // Someone else switched buffers while we were trying to obtain
        // the lock. Nothing to do here.
    }

    return true;
}

uint64_t* trace_context::AllocDurableRecord(size_t num_bytes) {
    ZX_DEBUG_ASSERT(UsingDurableBuffer());
    ZX_DEBUG_ASSERT((num_bytes & 7) == 0);

    uint64_t buffer_offset =
        durable_buffer_current_.fetch_add(num_bytes,
                                          std::memory_order_relaxed);
    if (likely(buffer_offset + num_bytes <= durable_buffer_size_)) {
        uint8_t* ptr = durable_buffer_start_ + buffer_offset;
        return reinterpret_cast<uint64_t*>(ptr); // success!
    }

    // Buffer is full!
    MarkDurableBufferFull(buffer_offset);

    return nullptr;
}

bool trace_context::AllocThreadIndex(trace_thread_index_t* out_index) {
    trace_thread_index_t index = next_thread_index_.fetch_add(1u, std::memory_order_relaxed);
    if (unlikely(index > TRACE_ENCODED_THREAD_REF_MAX_INDEX)) {
        // Guard again possible wrapping.
        next_thread_index_.store(TRACE_ENCODED_THREAD_REF_MAX_INDEX + 1u,
                                 std::memory_order_relaxed);
        return false;
    }
    *out_index = index;
    return true;
}

bool trace_context::AllocStringIndex(trace_string_index_t* out_index) {
    trace_string_index_t index = next_string_index_.fetch_add(1u, std::memory_order_relaxed);
    if (unlikely(index > TRACE_ENCODED_STRING_REF_MAX_INDEX)) {
        // Guard again possible wrapping.
        next_string_index_.store(TRACE_ENCODED_STRING_REF_MAX_INDEX + 1u,
                                 std::memory_order_relaxed);
        return false;
    }
    *out_index = index;
    return true;
}

void trace_context::ComputeBufferSizes() {
    size_t full_buffer_size = buffer_end_ - buffer_start_;
    ZX_DEBUG_ASSERT(full_buffer_size >= kMinPhysicalBufferSize);
    ZX_DEBUG_ASSERT(full_buffer_size <= kMaxPhysicalBufferSize);
    size_t header_size = sizeof(trace_buffer_header);
    switch (buffering_mode_) {
    case TRACE_BUFFERING_MODE_ONESHOT:
        // Create one big buffer, where durable and non-durable records share
        // the same buffer. There is no separate buffer for durable records.
        durable_buffer_start_ = nullptr;
        durable_buffer_size_ = 0;
        rolling_buffer_start_[0] = buffer_start_ + header_size;
        rolling_buffer_size_ = full_buffer_size - header_size;
        // The second rolling buffer is not used.
        rolling_buffer_start_[1] = nullptr;
        break;
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING: {
        // Rather than make things more complex on the user, at least for now,
        // we choose the sizes of the durable and rolling buffers.
        // Note: The durable buffer must have enough space for at least
        // the initialization record.
        // TODO(dje): The current choices are wip.
        uint64_t avail = full_buffer_size - header_size;
        uint64_t durable_buffer_size = GET_DURABLE_BUFFER_SIZE(avail);
        if (durable_buffer_size > kMaxDurableBufferSize)
            durable_buffer_size = kMaxDurableBufferSize;
        // Further adjust |durable_buffer_size| to ensure all buffers are a
        // multiple of 8. |full_buffer_size| is guaranteed by
        // |trace_start_engine()| to be a multiple of 4096. We only assume
        // header_size is a multiple of 8. In order for rolling_buffer_size
        // to be a multiple of 8 we need (avail - durable_buffer_size) to be a
        // multiple of 16. Round durable_buffer_size up as necessary.
        uint64_t off_by = (avail - durable_buffer_size) & 15;
        ZX_DEBUG_ASSERT(off_by == 0 || off_by == 8);
        durable_buffer_size += off_by;
        ZX_DEBUG_ASSERT((durable_buffer_size & 7) == 0);
        // The value of |kMinPhysicalBufferSize| ensures this:
        ZX_DEBUG_ASSERT(durable_buffer_size >= kMinDurableBufferSize);
        uint64_t rolling_buffer_size = (avail - durable_buffer_size) / 2;
        ZX_DEBUG_ASSERT((rolling_buffer_size & 7) == 0);
        // We need to maintain the invariant that the entire buffer is used.
        // This works if the buffer size is a multiple of
        // sizeof(trace_buffer_header), which is true since the buffer is a
        // vmo (some number of 4K pages).
        ZX_DEBUG_ASSERT(durable_buffer_size + 2 * rolling_buffer_size == avail);
        durable_buffer_start_ = buffer_start_ + header_size;
        durable_buffer_size_ = durable_buffer_size;
        rolling_buffer_start_[0] = durable_buffer_start_ + durable_buffer_size_;
        rolling_buffer_start_[1] = rolling_buffer_start_[0] + rolling_buffer_size;
        rolling_buffer_size_ = rolling_buffer_size;
        break;
    }
    default:
        __UNREACHABLE;
    }
}

void trace_context::ResetDurableBufferPointers() {
    durable_buffer_current_.store(0);
    durable_buffer_full_mark_.store(0);
}

void trace_context::ResetRollingBufferPointers() {
    rolling_buffer_current_.store(0);
    rolling_buffer_full_mark_[0].store(0);
    rolling_buffer_full_mark_[1].store(0);
}

void trace_context::ResetBufferPointers() {
    ResetDurableBufferPointers();
    ResetRollingBufferPointers();
}

void trace_context::InitBufferHeader() {
    memset(header_, 0, sizeof(*header_));

    header_->magic = TRACE_BUFFER_HEADER_MAGIC;
    header_->version = TRACE_BUFFER_HEADER_V0;
    header_->buffering_mode = static_cast<uint8_t>(buffering_mode_);
    header_->total_size = buffer_end_ - buffer_start_;
    header_->durable_buffer_size = durable_buffer_size_;
    header_->rolling_buffer_size = rolling_buffer_size_;
}

void trace_context::ClearEntireBuffer() {
    ResetBufferPointers();
    InitBufferHeader();
}

void trace_context::ClearRollingBuffers() {
    ResetRollingBufferPointers();
}

void trace_context::UpdateBufferHeaderAfterStopped() {
    // If the buffer filled, then the current pointer is "snapped" to the end.
    // Therefore in that case we need to use the buffer_full_mark.
    uint64_t durable_last_offset = durable_buffer_current_.load(std::memory_order_relaxed);
    uint64_t durable_buffer_full_mark = durable_buffer_full_mark_.load(std::memory_order_relaxed);
    if (durable_buffer_full_mark != 0)
        durable_last_offset = durable_buffer_full_mark;
    header_->durable_data_end = durable_last_offset;

    uint64_t offset_plus_counter =
        rolling_buffer_current_.load(std::memory_order_relaxed);
    uint64_t last_offset = GetBufferOffset(offset_plus_counter);
    uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
    header_->wrapped_count = wrapped_count;
    int buffer_number = GetBufferNumber(wrapped_count);
    uint64_t buffer_full_mark = rolling_buffer_full_mark_[buffer_number].load(std::memory_order_relaxed);
    if (buffer_full_mark != 0)
        last_offset = buffer_full_mark;
    header_->rolling_data_end[buffer_number] = last_offset;

    header_->num_records_dropped = num_records_dropped();
}

size_t trace_context::RollingBytesAllocated() const {
    switch (buffering_mode_) {
    case TRACE_BUFFERING_MODE_ONESHOT: {
        // There is a window during the processing of buffer-full where
        // |rolling_buffer_current_| may point beyond the end of the buffer.
        // This is ok, we don't promise anything better.
        uint64_t full_bytes = rolling_buffer_full_mark_[0].load(std::memory_order_relaxed);
        if (full_bytes != 0)
            return full_bytes;
        return rolling_buffer_current_.load(std::memory_order_relaxed);
    }
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING: {
        // Obtain the lock so that the buffers aren't switched on us while
        // we're trying to compute the total.
        std::lock_guard<std::mutex> lock(buffer_switch_mutex_);
        uint64_t offset_plus_counter =
            rolling_buffer_current_.load(std::memory_order_relaxed);
        uint32_t wrapped_count = GetWrappedCount(offset_plus_counter);
        int buffer_number = GetBufferNumber(wrapped_count);
        // Note: If we catch things at the point where the buffer has
        // filled, but before we swap buffers, then |buffer_offset| can point
        // beyond the end. This is ok, we don't promise anything better.
        uint64_t buffer_offset = GetBufferOffset(offset_plus_counter);
        if (wrapped_count == 0)
            return buffer_offset;
        // We've wrapped at least once, so the other buffer's "full mark"
        // must be set. However, it may be zero if streaming and we happened
        // to stop at a point where the buffer was saved, and hasn't
        // subsequently been written to.
        uint64_t full_mark_other_buffer = rolling_buffer_full_mark_[!buffer_number].load(std::memory_order_relaxed);
        return full_mark_other_buffer + buffer_offset;
    }
    default:
        __UNREACHABLE;
    }
}

size_t trace_context::DurableBytesAllocated() const {
    // Note: This will return zero in oneshot mode (as it should).
    uint64_t offset = durable_buffer_full_mark_.load(std::memory_order_relaxed);
    if (offset == 0)
        offset = durable_buffer_current_.load(std::memory_order_relaxed);
    return offset;
}

void trace_context::MarkDurableBufferFull(uint64_t last_offset) {
    // Snap to the endpoint to reduce likelihood of pointer wrap-around.
    // Otherwise each new attempt fill continually increase the offset.
    durable_buffer_current_.store(reinterpret_cast<uint64_t>(durable_buffer_size_),
                                  std::memory_order_relaxed);

    // Mark the end point if not already marked.
    uintptr_t expected_mark = 0u;
    if (durable_buffer_full_mark_.compare_exchange_strong(
            expected_mark, last_offset,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        fprintf(stderr, "TraceEngine: durable buffer full @offset %" PRIu64 "\n",
                last_offset);
        header_->durable_data_end = last_offset;

        // A record may be written that relies on this durable record.
        // To preserve data integrity, we disable all further tracing.
        // There is a small window where a non-durable record could get
        // emitted that depends on this durable record. It's rare
        // enough and inconsequential enough that we ignore it.
        // TODO(dje): Another possibility is we could let tracing
        // continue and start allocating future durable records in the
        // rolling buffers, and accept potentially lost durable
        // records. Another possibility is to remove the durable buffer,
        // and, say, have separate caches for each rolling buffer.
        MarkTracingArtificiallyStopped();
    }
}

void trace_context::MarkOneshotBufferFull(uint64_t last_offset) {
    SnapToEnd(0);

    // Mark the end point if not already marked.
    uintptr_t expected_mark = 0u;
    if (rolling_buffer_full_mark_[0].compare_exchange_strong(
            expected_mark, last_offset,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        header_->rolling_data_end[0] = last_offset;
    }

    MarkRecordDropped();
}

void trace_context::MarkRollingBufferFull(uint32_t wrapped_count, uint64_t last_offset) {
    // Mark the end point if not already marked.
    int buffer_number = GetBufferNumber(wrapped_count);
    uint64_t expected_mark = 0u;
    if (rolling_buffer_full_mark_[buffer_number].compare_exchange_strong(
            expected_mark, last_offset,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        header_->rolling_data_end[buffer_number] = last_offset;
    }
}

void trace_context::SwitchRollingBufferLocked(uint32_t prev_wrapped_count,
                                              uint64_t prev_last_offset) {
    // This has already done in streaming mode when the buffer was marked as
    // saved, but hasn't been done yet for circular mode. KISS and just do it
    // again. It's ok to do again as we don't resume allocating trace records
    // until we update |rolling_buffer_current_|.
    uint32_t new_wrapped_count = prev_wrapped_count + 1;
    int next_buffer = GetBufferNumber(new_wrapped_count);
    rolling_buffer_full_mark_[next_buffer].store(0, std::memory_order_relaxed);
    header_->rolling_data_end[next_buffer] = 0;

    // Do this last: After this tracing resumes in the new buffer.
    uint64_t new_offset_plus_counter = MakeOffsetPlusCounter(0, new_wrapped_count);
    rolling_buffer_current_.store(new_offset_plus_counter,
                                  std::memory_order_relaxed);
}

void trace_context::MarkTracingArtificiallyStopped() {
    // Grab the lock in part so that we don't switch buffers between
    // |CurrentWrappedCount()| and |SnapToEnd()|.
    std::lock_guard<std::mutex> lock(buffer_switch_mutex_);

    // Disable tracing by making it look like the current rolling
    // buffer is full. AllocRecord, on seeing the buffer is full, will
    // then check |tracing_artificially_stopped_|.
    tracing_artificially_stopped_ = true;
    SnapToEnd(CurrentWrappedCount());
}

void trace_context::NotifyRollingBufferFullLocked(uint32_t wrapped_count,
                                                  uint64_t durable_data_end) {
    // The notification is handled on the engine's event loop as
    // we need this done outside of the lock: Certain handlers
    // (e.g., trace-benchmark) just want to immediately call
    // |trace_engine_mark_buffer_saved()| which wants to reacquire
    // the lock. Secondly, if we choose to wait until the buffer context is
    // released before notifying the handler then we can't do so now as we
    // still have a reference to the buffer context.
    trace_engine_request_save_buffer(wrapped_count, durable_data_end);
}

void trace_context::HandleSaveRollingBufferRequest(uint32_t wrapped_count,
                                                   uint64_t durable_data_end) {
    // TODO(dje): An open issue is solving the problem of TraceManager
    // prematurely reading the buffer: We know the buffer is full, but
    // the only way we know existing writers have completed is when
    // they release their trace context. Fortunately we know when all
    // context acquisitions for the purpose of writing to the buffer
    // have been released. The question is how to use this info.
    // For now we punt the problem to the handler. Ultimately we could
    // provide callers with a way to wait, and have trace_release_context()
    // check for waiters and if any are present send a signal like it does
    // for SIGNAL_CONTEXT_RELEASED.
    handler_->ops->notify_buffer_full(handler_, wrapped_count,
                                      durable_data_end);
}

void trace_context::MarkRollingBufferSaved(uint32_t wrapped_count,
                                           uint64_t durable_data_end) {
    std::lock_guard<std::mutex> lock(buffer_switch_mutex_);

    int buffer_number = GetBufferNumber(wrapped_count);
    {
        // TODO(dje): Manage bad responses from TraceManager.
        int current_buffer_number = GetBufferNumber(GetWrappedCount(rolling_buffer_current_.load(std::memory_order_relaxed)));
        ZX_DEBUG_ASSERT(buffer_number != current_buffer_number);
    }
    rolling_buffer_full_mark_[buffer_number].store(0, std::memory_order_relaxed);
    header_->rolling_data_end[buffer_number] = 0;
    // Don't update |rolling_buffer_current_| here, that is done when we
    // successfully allocate the next record. Until then we want to keep
    // dropping records.
}
