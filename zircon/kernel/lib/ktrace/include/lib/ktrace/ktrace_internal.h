// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_

#include <assert.h>
#include <lib/fit/function.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/result.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <ktl/atomic.h>
#include <ktl/forward.h>

// Fwd decl of tests to allow friendship.
namespace ktrace_tests {
class TestKTraceState;
}

namespace internal {

class KTraceState {
 public:
  ////////////////////////////////////////////////////////////////
  //
  // Notes on KTrace operating modes.
  //
  // KTrace can currently operate in one of two different modes, either
  // "Saturate" or "Circular".
  //
  // During saturating operation, if an attempt is made to write a record to the
  // ktrace buffer, but there is not enough room to write the record, then the
  // buffer has become "saturated".  The record is dropped, and the group mask
  // is cleared, preventing new writes from occurring until the trace is
  // restarted.
  //
  // During circular operation, if an attempt is made to write a record to the
  // ktrace buffer, but there is not enough room to write the record, then old
  // records are discarded from the trace buffer in order to make room for new
  // records.
  //
  // After a rewind operation, but before starting, the buffer is effectively
  // operating in saturating mode for the purposes of recording static data such
  // as the names of probes and threads in the system at the start of tracing.
  // Afterwards, if the trace is then started in circular mode, the KTraceState
  // instance remembers the point in the buffer where the static records ended,
  // and the circular portion of the buffer starts.  Records from the static
  // region of the trace will never be purged from the trace to make room for
  // new records recorded while in circular mode.
  //
  // A trace may be started, stopped, and started again in Saturate mode any
  // number of times without rewinding.  Additionally, a trace which has
  // previously been started in Saturate mode may subsequently be started in
  // Circular mode without rewinding.  All records recorded while in saturate
  // mode will be part of the static region of the buffer.  It is, however, not
  // legal to start a trace in Circular mode, then stop it, and then attempt to
  // start it again in Saturate mode.
  enum class StartMode { Saturate, Circular };

  constexpr KTraceState() = default;
  virtual ~KTraceState();

  // Initialize the KTraceState instance, may only be called once.  Any methods
  // called on a KTraceState instance after construction, but before Init,
  // should behave as no-ops.
  //
  // |target_bufsize| : The target size (in bytes) of the ktrace buffer to be
  // allocated.  Must be a multiple of 8 bytes.
  //
  // |initial_groups| : The initial set of enabled trace groups (see
  // zircon-internal/ktrace.h).  If non-zero, causes Init to attempt to allocate
  // the trace buffer immediately.  If the allocation fails, or the initial
  // group mask is zero, allocation is delayed until the first time that start
  // is called.
  //
  void Init(uint32_t target_bufsize, uint32_t initial_groups) TA_EXCL(lock_, write_lock_);

  [[nodiscard]] zx_status_t Start(uint32_t groups, StartMode mode) TA_EXCL(lock_, write_lock_);
  [[nodiscard]] zx_status_t Stop() TA_EXCL(lock_, write_lock_);
  [[nodiscard]] zx_status_t Rewind() TA_EXCL(lock_, write_lock_) {
    Guard<Mutex> guard(&lock_);
    return RewindLocked();
  }

  ssize_t ReadUser(user_out_ptr<void> ptr, uint32_t off, size_t len) TA_EXCL(lock_, write_lock_);

  // Write a record to the tracelog.
  //
  // |payload| must consist of all uint32_t or all uint64_t types.
  template <typename... Args>
  void WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, Args... args);
  void WriteRecordTiny(uint32_t tag, uint32_t arg) TA_EXCL(write_lock_);
  void WriteNameEtc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always)
      TA_EXCL(write_lock_);

  inline uint32_t grpmask() const {
    return static_cast<uint32_t>(grpmask_and_inflight_writes_.load(ktl::memory_order_acquire));
  }

  // Check to see if a tag is currently enabled using either a new observation
  // of the group mask (default), or a previous observation.
  static inline bool tag_enabled(uint32_t tag, uint32_t mask) { return (mask & tag) != 0; }

  inline bool tag_enabled(uint32_t tag) const { return tag_enabled(tag, grpmask()); }

  // Temporary (fxbug.dev/98176): A small wrapper for writing a single
  // FXT-in-KTrace record.
  //
  // We allow the calling code to specify a KTrace group and event type for the
  // benefit of ktrace_provider's processing during the transition to full FXT.
  // The computed record size from the header passed by libfxt and the rest of
  // the KTrace tag are combined to create the tag for the KTrace reservation,
  // which is then used as a buffer for the FXT writing.
  //
  // This wrapper is used for writing a single FXT record, and should be
  // discarded after the write is complete. For writing multiple records, create
  // a separate instance for each record.
  class FxtCompatWriter {
   public:
    FxtCompatWriter(KTraceState& ks, uint32_t tag) : ks_(ks), tag_(tag) {}

    class Reservation {
     public:
      Reservation(uint64_t* ptr, uint64_t ktrace_header)
          : ptr_(ptr), ktrace_header_(ktrace_header) {}

      void WriteWord(uint64_t word);
      void WriteBytes(const void* bytes, size_t num_bytes);
      void Commit();

     private:
      uint64_t* ptr_{nullptr};
      size_t word_offset_{1};
      uint64_t ktrace_header_{0};
    };

    zx::result<Reservation> Reserve(uint64_t header);

   private:
    KTraceState& ks_;
    const uint32_t tag_;
  };

  inline FxtCompatWriter make_fxt_writer(uint32_t tag) { return FxtCompatWriter(*this, tag); }

 private:
  // A small RAII helper which makes sure that we don't mess up our
  // in_flight_writes bookkeeping.
  class AutoWriteInFlight {
   public:
    explicit AutoWriteInFlight(KTraceState& ks)
        : ks_(ks),
          observed_grpmask_(
              static_cast<uint32_t>(ks_.grpmask_and_inflight_writes_.fetch_add(
                                        kInflightWritesInc, ktl::memory_order_acq_rel) &
                                    ~kInflightWritesMask)) {}

    ~AutoWriteInFlight() {
      [[maybe_unused]] uint64_t prev;
      prev =
          ks_.grpmask_and_inflight_writes_.fetch_sub(kInflightWritesInc, ktl::memory_order_release);
      DEBUG_ASSERT((prev & kInflightWritesMask) > 0);
    }

    uint32_t observed_grpmask() const { return observed_grpmask_; }

   private:
    KTraceState& ks_;
    const uint32_t observed_grpmask_;
  };

  // A small helper class which should make it impossible to forget to commit a
  // record after a successful reservation.
  class PendingCommit {
   public:
    // There are only two ways to make an instance of a PendingCommit.  Either
    // via implicit conversion from nullptr (a failed reservation), or from a
    // pointer to the start of the record, and a value for the tag which
    // eventually must be committed.
    PendingCommit(nullptr_t) {}
    PendingCommit(void* ptr, uint32_t tag) : ptr_(ptr), tag_(tag) {}

    // No copy.
    PendingCommit(const PendingCommit&) = delete;
    PendingCommit& operator=(const PendingCommit&) = delete;

    // Yes move.
    PendingCommit(PendingCommit&& other) noexcept : ptr_(other.ptr_), tag_(other.tag_) {
      other.ptr_ = nullptr;
    }

    PendingCommit& operator=(PendingCommit&& other) noexcept {
      ptr_ = other.ptr_;
      tag_ = other.tag_;
      other.ptr_ = nullptr;
      return *this;
    }

    // Going out of scope is what triggers the commit.
    ~PendingCommit() {
      if (ptr_ != nullptr) {
        ktl::atomic_ref(*static_cast<uint32_t*>(ptr_)).store(tag_, ktl::memory_order_release);
      }
    }

    // Users need access to the reserved pointer in order to fill out their
    // record payload.
    ktrace_header_t* hdr() const { return reinterpret_cast<ktrace_header_t*>(ptr_); }
    bool is_valid() const { return (ptr_ != nullptr); }

   private:
    void* ptr_{nullptr};
    uint32_t tag_{0};
  };

  friend class ktrace_tests::TestKTraceState;
  friend class AutoWriteInFlight;

  static inline uint32_t MakeTidField(uint32_t tag) {
    return KTRACE_FLAGS(tag) & KTRACE_FLAGS_CPU
               ? arch_curr_cpu_num()
               : static_cast<uint32_t>(Thread::Current::Get()->tid());
  }

  [[nodiscard]] zx_status_t RewindLocked() TA_REQ(lock_);

  // Add static names (eg syscalls and probes) to the trace buffer.  Called
  // during a rewind operation immediately after resetting the trace buffer.
  // Declared as virtual to facilitate testing.
  virtual void ReportStaticNames() TA_REQ(lock_);

  // Add the names of current live threads and processes to the trace buffer.
  // Called during start operations just before setting the group mask. Declared
  // as virtual to facilitate testing.
  virtual void ReportThreadProcessNames() TA_REQ(lock_);

  // Copy data from kernel memory to user memory.  Used by Read, and overloaded
  // by test code (which needs to copy to kernel memory, not user memory).
  virtual zx_status_t CopyToUser(user_out_ptr<uint8_t> dst, const uint8_t* src, size_t len) {
    return dst.copy_array_to_user(src, len);
  }

  // A small printf stand-in which gives tests the ability to disable diagnostic
  // printing during testing.
  int DiagsPrintf(int level, const char* fmt, ...) __PRINTFLIKE(3, 4) {
    if (!disable_diags_printfs_ && DPRINTF_ENABLED_FOR_LEVEL(level)) {
      va_list args;
      va_start(args, fmt);
      int result = vprintf(fmt, args);
      va_end(args);
      return result;
    }

    return 0;
  }

  // Attempt to allocate our buffer, if we have not already done so.
  zx_status_t AllocBuffer() TA_REQ(lock_);

  // Reserve KTRACE_LEN(tag) bytes of contiguous space in the buffer, if
  // possible.
  PendingCommit Reserve(uint32_t tag);
  // Reserve the specified number of bytes in the buffer, if possible, without
  // the PendingCommit wrapper.
  void* ReserveRaw(uint32_t num_bytes);

  inline void DisableGroupMask() {
    grpmask_and_inflight_writes_.fetch_and(kInflightWritesMask, ktl::memory_order_release);
  }

  inline void SetGroupMask(uint32_t new_mask) {
    grpmask_and_inflight_writes_.fetch_and(kInflightWritesMask, ktl::memory_order_relaxed);
    grpmask_and_inflight_writes_.fetch_or(new_mask, ktl::memory_order_release);
  }

  // Convert an absolute read or write pointer into an offset into the circular
  // region of the buffer.  Note that it is illegal to call this if we are not
  // operating in circular mode.
  uint32_t PtrToCircularOffset(uint64_t ptr) const TA_REQ(write_lock_) {
    DEBUG_ASSERT(circular_size_ > 0);
    return static_cast<uint32_t>((ptr % circular_size_) + wrap_offset_);
  }

  inline uint32_t inflight_writes() const {
    return static_cast<uint32_t>(
        (grpmask_and_inflight_writes_.load(ktl::memory_order_acquire) & kInflightWritesMask) >> 32);
  }

  // Allow diagnostic dprintf'ing or not.  Overridden by test code.
  bool disable_diags_printfs_{false};

  // An atomic state variable which tracks the currently active group mask (in
  // its lower 32 bits) and the current in-flight write count (in its upper 32
  // bits).
  //
  // Write operations consist of:
  //
  // 1) Observing the group mask with acquire semantics to determine if the
  //    write should proceed.
  // 2) Incrementing the in-flight-write count portion of the state with acq/rel
  //    semantics to indicate that a write operation has begun.
  // 3) Completing the operation, or aborting it if the group mask has been
  //    disabled for this write since step #1.
  // 4) Decrementing the in-flight-write count portion of the state with release
  //    semantics to indicate that the write is finished.
  //
  // This allows Stop operations to synchronize with any in-flight writes by:
  //
  // 1) Clearing the grpmask portion of the state with release semantics.
  // 2) Spinning on the in-flight-writes portion of the mask with acquire
  //    semantics until an in-flight count of zero is observed.
  //
  static constexpr uint64_t kInflightWritesMask = 0xFFFFFFFF00000000;
  static constexpr uint64_t kInflightWritesInc = 0x0000000100000000;
  ktl::atomic<uint64_t> grpmask_and_inflight_writes_{0};

  // The target buffer size (in bytes) we would like to use, when we eventually
  // call AllocBuffer.  Set during the call to Init.
  uint32_t target_bufsize_{0};

  // A lock used to serialize all non-write operations.  IOW - this lock ensures
  // that only a single thread at a time may be involved in operations such as
  // Start, Stop, Rewind, and ReadUser
  DECLARE_MUTEX(KTraceState) lock_;
  bool is_started_ TA_GUARDED(lock_){false};

  // The core allocation state of the trace buffer, protected by the write
  // spinlock.  See "Notes on KTrace operating modes" (above) for details on
  // saturate vs. circular mode.  This comment will describe how the bookkeeping
  // maintained in each of the two modes, how wrapping is handled in circular
  // mode, and how space for records in the buffer is reserved and subsequently
  // committed.
  //
  // --== Saturate mode ==--
  //
  // While operating in saturate mode, the value of |circular_size_| and |rd_|
  // will always be 0, and the value of |wrap_offset_| is not defined.  The only
  // important piece of bookkeeping maintained is the value of |wr_|.  |wr_|
  // always points to the offset in the buffer where the next record will be
  // stored, and it should always be <= |bufsize_|.  When reading back records,
  // the first record will always be located at offset 0.
  //
  // --== Circular mode ==--
  //
  // When operating in circular mode, the buffer is partitioned into two
  // regions; a "static" region which contains the records recorded before
  // entering circular mode, and a circular region which contain records written
  // after beginning circular operation.  |circular_size_| must be non-zero, and
  // contains the size (in bytes) of the circular region of the buffer.  The
  // region of the buffer from [0, wrap_offset_) is the static region of the
  // buffer, while the region from [wrap_offset_, bufsize_) is the circular
  // region.  |wrap_offset_| must always be < |bufsize_|.
  //
  // The |rd_| and |wr_| pointers are absolute offsets into the circular region
  // of the buffer, modulo |circular_size_|.  When space in the buffer is
  // reserved for a record, |wr_| is incremented by the size of the record.
  // When a record is purged to make room for new records, |rd_| is incremented.
  // At all times, |rd_| <= |wr_|, and both pointers are monotonically
  // increasing.  The function which maps from one of these pointers to an
  // offset in the buffer (on the range [0, bufsize_)) is given by
  //
  //   f(ptr) = (ptr % circular_size_) + wrap_offset_
  //
  // --== Reserving records and memory ordering ==--
  //
  // In order to write a record to the trace buffer, the writer must first
  // reserve the space to do so.  During this period of time, the |write_lock_|
  // is held while the bookkeeping is handled in order to reserve space.
  // Holding the write lock during reservation guarantees coherent observations
  // of the bookkeeping state by the writers.
  //
  // If the reservation succeeds, the tag field of the reserved record is stored
  // as 0 with release semantics, then the write lock is dropped in order to
  // allow other reservations to take place concurrently while the payload of
  // the record is populated.  Once the writer has finished recording the
  // payload, it must write the final tag value for the record with release
  // semantics.  This finalizes the record, and after this operation, the
  // payload may no longer change.
  //
  // If, while operating in circular mode, an old record needs to be purged in
  // order to make space for a new record, the |rd_| pointer will simply be
  // incremented by the size of the record located at the |rd_| pointer.  The
  // tag of this record must first be read with memory order acquire semantics
  // in order to compute its length so that the |rd_| pointer may be adjusted
  // appropriately.  If, during this observation, the value of the tag is
  // observed to be 0, it means that a writer is attempting to advance the read
  // pointer past a record which has not been fully committed yet.  If this ever
  // happens, the reservation operation fails, and the group mask will be
  // cleared, just like if a reservation had failed in saturating mode.
  //
  // --== Circular mode padding ==--
  //
  // If a record of size X is to be reserved in the trace buffer while operating
  // in circular mode, and the distance between the write pointer and the end of
  // the buffer is too small for the record to be contained contiguously, a
  // "padding" record will be inserted instead.  This is a record with a group
  // ID of 0 which contains no payload.  Its only purpose is to bad the buffer
  // out so that the record to be written may exist contiguously in the trace
  // buffer.
  //
  DECLARE_SPINLOCK(KTraceState) write_lock_;
  uint64_t rd_ TA_GUARDED(write_lock_){0};
  uint64_t wr_ TA_GUARDED(write_lock_){0};
  uint32_t circular_size_ TA_GUARDED(write_lock_){0};
  uint32_t wrap_offset_ TA_GUARDED(write_lock_){0};

  // Note: these don't _actually_ have to be protected by the write lock.
  // Memory ordering consistency for mutators of these variables are protected
  // via lock_, while observations from trace writers are actually protected by
  // a complicated set of arguments based on the stopped/started state of the
  // system, and the acq/rel semantics of the grpmask_ variable.
  //
  // Instead of relying on these complicated and difficult to
  // communicate/enforce invariants, however, we just toss these variables into
  // the write lock and leave it at that.  Trace writers already needed to be
  // inside of the write lock to manipulate the read/write pointers while
  // reserving space.  Mutation of these variables can only happen during
  // start/init when the system is stopped (and there are no writers), so
  // obtaining the write lock to allocate the buffer is basically free since it
  // will never be contested.
  //
  uint8_t* buffer_ TA_GUARDED(write_lock_){nullptr};
  uint32_t bufsize_ TA_GUARDED(write_lock_){0};
};

}  // namespace internal

#endif  // ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_
