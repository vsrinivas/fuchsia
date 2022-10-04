// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_CONTEXT_IMPL_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_CONTEXT_IMPL_H_

#include <atomic>
#include <mutex>

#include <zircon/assert.h>

#include <lib/trace-engine/buffer_internal.h>
#include <lib/trace-engine/context.h>
#include <lib/trace-engine/handler.h>
#include <lib/zx/event.h>

// Two preprocessor symbols control what symbols we export in a .so:
// EXPORT and EXPORT_NO_DDK:
// - EXPORT is for symbols exported to both driver and non-driver versions of
//   the library ("non-driver" is the normal case).
// - EXPORT_NO_DDK is for symbols *not* exported in the DDK.
// A third variant is supported which is to export nothing. This is for cases
// like libvulkan which want tracing but do not have access to
// libtrace-engine.so.
// Two preprocessor symbols are provided by the build system to select which
// variant we are building: STATIC_LIBRARY. If it is not defined
// (normal case), or exactly one of them is defined.
#if defined(STATIC_LIBRARY)
#define EXPORT
#define EXPORT_NO_DDK
#else
#define EXPORT __EXPORT
#define EXPORT_NO_DDK __EXPORT
#endif

using trace::internal::trace_buffer_header;

// Return true if there are no buffer acquisitions of the trace context.
bool trace_engine_is_buffer_context_released();

// Called from trace_context to notify the engine a buffer needs saving.
void trace_engine_request_save_buffer(uint32_t wrapped_count, uint64_t durable_data_end);

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

  static size_t MaxUsableBufferOffset() {
    return (1ull << kUsableBufferOffsetBits) - sizeof(uint64_t);
  }

  uint32_t generation() const { return generation_; }

  trace_handler_t* handler() const { return handler_; }

  trace_buffering_mode_t buffering_mode() const { return buffering_mode_; }

  uint64_t num_records_dropped() const {
    return num_records_dropped_.load(std::memory_order_relaxed);
  }

  bool UsingDurableBuffer() const { return buffering_mode_ != TRACE_BUFFERING_MODE_ONESHOT; }

  // Return true if at least one record was dropped.
  bool WasRecordDropped() const { return num_records_dropped() != 0u; }

  // Return the number of bytes currently allocated in the rolling buffer(s).
  size_t RollingBytesAllocated() const;

  size_t DurableBytesAllocated() const;

  void ResetDurableBufferPointers();
  void ResetRollingBufferPointers();
  void ResetBufferPointers();
  void InitBufferHeader();
  void ClearEntireBuffer();
  void ClearRollingBuffers();
  void UpdateBufferHeaderAfterStopped();

  uint64_t* AllocRecord(size_t num_bytes);
  uint64_t* AllocDurableRecord(size_t num_bytes);
  bool AllocThreadIndex(trace_thread_index_t* out_index);
  bool AllocStringIndex(trace_string_index_t* out_index);

  // This is called by the handler when it has been notified that a buffer
  // has been saved.
  // |wrapped_count| is the wrapped count at the time the buffer save request
  // was made. Similarly for |durable_data_end|.
  void MarkRollingBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end);

  // This is only called from the engine to initiate a buffer save.
  void HandleSaveRollingBufferRequest(uint32_t wrapped_count, uint64_t durable_data_end);

 private:
  // The maximum rolling buffer size in bits.
  static constexpr size_t kRollingBufferSizeBits = 32;

  // Maximum size, in bytes, of a rolling buffer.
  static constexpr size_t kMaxRollingBufferSize = 1ull << kRollingBufferSizeBits;

  // The number of usable bits in the buffer pointer.
  // This is several bits more than the maximum buffer size to allow a
  // buffer pointer to grow without overflow while TraceManager is saving a
  // buffer in streaming mode.
  // In this case we don't snap the offset to the end as doing so requires
  // modifying state and thus obtaining the lock (streaming mode is not
  // lock-free). Instead the offset keeps growing.
  // kUsableBufferOffsetBits = 40 bits = 1TB.
  // Max rolling buffer size = 32 bits = 4GB.
  // Thus we assume TraceManager can save 4GB of trace before the client
  // writes 1TB of trace data (lest the offset part of
  // |rolling_buffer_current_| overflows). But, just in case, if
  // TraceManager still can't keep up we stop tracing when the offset
  // approaches overflowing. See AllocRecord().
  static constexpr int kUsableBufferOffsetBits = kRollingBufferSizeBits + 8;

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
  static constexpr size_t kMaxPhysicalBufferSize = kMaxRollingBufferSize;

  // The minimum size of the durable buffer.
  // There must be enough space for at least the initialization record.
  static constexpr size_t kMinDurableBufferSize = 16;

  // The maximum size of the durable buffer.
  // We need enough space for:
  // - initialization record = 16 bytes
  // - string table (max TRACE_ENCODED_STRING_REF_MAX_INDEX = 0x7fffu entries)
  // - thread table (max TRACE_ENCODED_THREAD_REF_MAX_INDEX = 0xff entries)
  // String entries are 8 bytes + length-round-to-8-bytes.
  // Strings have a max size of TRACE_ENCODED_STRING_REF_MAX_LENGTH bytes
  // = 32000. We assume most are < 64 bytes.
  // Thread entries are 8 bytes + pid + tid = 24 bytes.
  // If we assume 10000 registered strings, typically 64 bytes, plus max
  // number registered threads, that works out to:
  // 16 /*initialization record*/
  // + 10000 * (8 + 64) /*strings*/
  // + 255 * 24 /*threads*/
  // = 726136.
  // We round this up to 1MB.
  static constexpr size_t kMaxDurableBufferSize = 1024 * 1024;

  // Given a buffer of size |SIZE| in bytes, not including the header,
  // return how much to use for the durable buffer. This is further adjusted
  // to be at most |kMaxDurableBufferSize|, and to account for rolling
  // buffer size alignment constraints.
#define GET_DURABLE_BUFFER_SIZE(size) ((size) / 16)

  // Ensure the smallest buffer is still large enough to hold
  // |kMinDurableBufferSize|.
  static_assert(GET_DURABLE_BUFFER_SIZE(kMinPhysicalBufferSize - sizeof(trace_buffer_header)) >=
                    kMinDurableBufferSize,
                "");

  static uintptr_t GetBufferOffset(uint64_t offset_plus_counter) {
    return offset_plus_counter & ((1ul << kBufferOffsetBits) - 1);
  }

  static uint32_t GetWrappedCount(uint64_t offset_plus_counter) {
    return static_cast<uint32_t>(offset_plus_counter >> kWrappedCounterShift);
  }

  static uint64_t MakeOffsetPlusCounter(uintptr_t offset, uint32_t counter) {
    return offset | (static_cast<uint64_t>(counter) << kWrappedCounterShift);
  }

  static int GetBufferNumber(uint32_t wrapped_count) { return wrapped_count & 1; }

  bool IsDurableBufferFull() const {
    return durable_buffer_full_mark_.load(std::memory_order_relaxed) != 0;
  }

  // Return true if |buffer_number| is ready to be written to.
  bool IsRollingBufferReady(int buffer_number) const {
    return rolling_buffer_full_mark_[buffer_number].load(std::memory_order_relaxed) == 0;
  }

  // Return true if the other rolling buffer is ready to be written to.
  bool IsOtherRollingBufferReady(int buffer_number) const {
    return IsRollingBufferReady(!buffer_number);
  }

  uint32_t CurrentWrappedCount() const {
    auto current = rolling_buffer_current_.load(std::memory_order_relaxed);
    return GetWrappedCount(current);
  }

  void ComputeBufferSizes();

  void MarkDurableBufferFull(uint64_t last_offset);

  void MarkOneshotBufferFull(uint64_t last_offset);

  void MarkRollingBufferFull(uint32_t wrapped_count, uint64_t last_offset);

  bool SwitchRollingBuffer(uint32_t wrapped_count, uint64_t buffer_offset);

  void SwitchRollingBufferLocked(uint32_t prev_wrapped_count, uint64_t prev_last_offset)
      __TA_REQUIRES(buffer_switch_mutex_);

  void StreamingBufferFullCheck(uint32_t wrapped_count, uint64_t buffer_offset);

  void MarkTracingArtificiallyStopped();

  void SnapToEnd(uint32_t wrapped_count) {
    // Snap to the endpoint for simplicity.
    // Several threads could all hit buffer-full with each one
    // continually incrementing the offset.
    uint64_t full_offset_plus_counter = MakeOffsetPlusCounter(rolling_buffer_size_, wrapped_count);
    rolling_buffer_current_.store(full_offset_plus_counter, std::memory_order_relaxed);
  }

  void MarkRecordDropped() { num_records_dropped_.fetch_add(1, std::memory_order_relaxed); }

  void NotifyRollingBufferFullLocked(uint32_t wrapped_count, uint64_t durable_data_end)
      __TA_REQUIRES(buffer_switch_mutex_);

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

  // Rolling buffer start.
  // To simplify switching between them we don't record the buffer end,
  // and instead record their size (which is identical).
  uint8_t* rolling_buffer_start_[2];

  // The size of both rolling buffers.
  size_t rolling_buffer_size_;

  // Current allocation pointer for durable records.
  // This only used in circular and streaming modes.
  // Starts at |durable_buffer_start| and grows from there.
  // May exceed |durable_buffer_end| when the buffer is full.
  std::atomic<uint64_t> durable_buffer_current_;

  // Offset beyond the last successful allocation, or zero if not full.
  // This only used in circular and streaming modes: There is no separate
  // buffer for durable records in oneshot mode.
  // Only ever set to non-zero once in the lifetime of the trace context.
  std::atomic<uint64_t> durable_buffer_full_mark_;

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
  std::atomic<uint64_t> rolling_buffer_current_;

  // Offset beyond the last successful allocation, or zero if not full.
  // Only ever set to non-zero once when the buffer fills.
  // This will only be set in oneshot and streaming modes.
  std::atomic<uint64_t> rolling_buffer_full_mark_[2];

  // A count of the number of records that have been dropped.
  std::atomic<uint64_t> num_records_dropped_{0};

  // A count of the number of records that have been dropped.
  std::atomic<uint64_t> num_records_dropped_after_buffer_switch_{0};

  // Set to true if the engine needs to stop tracing for some reason.
  bool tracing_artificially_stopped_ __TA_GUARDED(buffer_switch_mutex_) = false;

  // This is used when switching rolling buffers.
  // It's a relatively rare operation, and this simplifies reasoning about
  // correctness.
  mutable std::mutex buffer_switch_mutex_;  // TODO(dje): more guards?

  // Handler associated with the trace session.
  trace_handler_t* const handler_;

  // The next thread index to be assigned.
  std::atomic<trace_thread_index_t> next_thread_index_{TRACE_ENCODED_THREAD_REF_MIN_INDEX};

  // The next string table index to be assigned.
  std::atomic<trace_string_index_t> next_string_index_{TRACE_ENCODED_STRING_REF_MIN_INDEX};
};

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_ENGINE_CONTEXT_IMPL_H_
