// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/boot-options/boot-options.h>
#include <lib/ktrace.h>
#include <lib/ktrace/string_ref.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <arch/user_copy.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/ktrace.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <lk/init.h>
#include <object/thread_dispatcher.h>
#include <vm/vm_aspace.h>

// The global ktrace state.
internal::KTraceState KTRACE_STATE;

namespace {

// One of these macros is invoked by kernel.inc for each syscall.

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) [ZX_SYS_##name] = #name,
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
#endif
constexpr const char* kSyscallNames[] = {
#include <lib/syscalls/kernel.inc>
};
#if defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL

void ktrace_report_syscalls() {
  for (uint32_t i = 0; i < ktl::size(kSyscallNames); ++i) {
    ktrace_name_etc(TAG_SYSCALL_NAME, i, 0, kSyscallNames[i], true);
  }
}

zx_ticks_t ktrace_ticks_per_ms() { return ticks_per_second() / 1000; }

StringRef* ktrace_find_probe(const char* name) {
  for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
    if (!strcmp(name, ref->string)) {
      return ref;
    }
  }
  return nullptr;
}

void ktrace_add_probe(StringRef* string_ref) {
  // Register and emit the string ref.
  string_ref->GetId();
}

void ktrace_report_probes() {
  for (StringRef* ref = StringRef::head(); ref != nullptr; ref = ref->next) {
    ktrace_name_etc(TAG_PROBE_NAME, ref->id, 0, ref->string, true);
  }
}

}  // namespace

namespace internal {

KTraceState::~KTraceState() {
  if (buffer_ != nullptr) {
    VmAspace* aspace = VmAspace::kernel_aspace();
    aspace->FreeRegion(reinterpret_cast<vaddr_t>(buffer_));
  }
}

void KTraceState::Init(uint32_t target_bufsize, uint32_t initial_groups) {
  Guard<Mutex> guard(&lock_);
  ASSERT_MSG(target_bufsize_ == 0,
             "Double init of KTraceState instance (tgt_bs %u, new tgt_bs %u)!", target_bufsize_,
             target_bufsize);
  ASSERT(is_started_ == false);

  // Allocations are rounded up to the nearest page size.
  target_bufsize_ = fbl::round_up(target_bufsize, static_cast<uint32_t>(PAGE_SIZE));

  if (initial_groups != 0) {
    if (AllocBuffer() == ZX_OK) {
      ReportThreadProcessNames();
      is_started_ = true;
    }
  }

  SetGroupMask(KTRACE_GRP_TO_MASK(initial_groups));
}

zx_status_t KTraceState::Start(uint32_t groups, StartMode mode) {
  Guard<Mutex> guard(&lock_);

  if (groups == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (zx_status_t status = AllocBuffer(); status != ZX_OK) {
    return status;
  }

  // If we are attempting to start in saturating mode, then check to be sure
  // that we were not previously operating in circular mode.  It is not legal to
  // re-start a ktrace buffer in saturating mode which had been operating in
  // circular mode.
  if (mode == StartMode::Saturate) {
    Guard<SpinLock, IrqSave> write_guard{&write_lock_};
    if (circular_size_ != 0) {
      return ZX_ERR_BAD_STATE;
    }
  }

  // If we are not yet started, we need to report the current thread and process
  // names.
  if (!is_started_) {
    ReportThreadProcessNames();
  }

  // If we are changing from saturating mode, to circular mode, we need to
  // update our circular bookkeeping.
  {
    Guard<SpinLock, IrqSave> write_guard{&write_lock_};
    if ((mode == StartMode::Circular) && (circular_size_ == 0)) {
      // Mark the point at which the static data ends and the circular
      // portion of the buffer starts (the "wrap offset").
      DEBUG_ASSERT(wr_ <= bufsize_);
      wrap_offset_ = static_cast<uint32_t>(ktl::min<uint64_t>(bufsize_, wr_));
      circular_size_ = bufsize_ - wrap_offset_;
      wr_ = 0;
    }
  }

  is_started_ = true;
  SetGroupMask(KTRACE_GRP_TO_MASK(groups));

  return ZX_OK;
}

zx_status_t KTraceState::Stop() {
  Guard<Mutex> guard(&lock_);

  // Start by setting the group mask to 0.  This should prevent any new
  // writers from starting write operations.  The non-write lock should
  // prevent anyone else from writing to this field while we are finishing
  // the stop operation.
  DisableGroupMask();

  // Now wait until any lingering write operations have finished.  This
  // should never take any significant amount of time.  If it does, we are
  // probably operating in a virtual environment with a host who is being
  // mean to us.
  zx_time_t absolute_timeout = current_time() + ZX_SEC(1);
  bool stop_synced;
  do {
    stop_synced = inflight_writes() == 0;
    if (!stop_synced) {
      Thread::Current::SleepRelative(ZX_MSEC(1));
    }
  } while (!stop_synced && (current_time() < absolute_timeout));

  if (!stop_synced) {
    return ZX_ERR_TIMED_OUT;
  }

  // Great, we are now officially stopped.  Record this.
  is_started_ = false;
  return ZX_OK;
}

zx_status_t KTraceState::RewindLocked() {
  if (is_started_) {
    return ZX_ERR_BAD_STATE;
  }

  DEBUG_ASSERT(grpmask_and_inflight_writes_.load(ktl::memory_order_acquire) == 0);

  {
    Guard<SpinLock, IrqSave> write_guard{&write_lock_};

    // roll back to just after the metadata
    rd_ = 0;
    wr_ = KTRACE_RECSIZE * 2;

    // After a rewind, we are no longer in circular buffer mode.
    wrap_offset_ = 0;
    circular_size_ = 0;

    // We cannot add metadata rewind if we have not allocated a buffer yet.
    if (buffer_ == nullptr) {
      wr_ = 0;
      return ZX_OK;
    }

    // Stash our version and timestamp resolution.
    uint64_t n = ktrace_ticks_per_ms();
    ktrace_rec_32b_t* rec = reinterpret_cast<ktrace_rec_32b_t*>(buffer_);
    rec[0].tag = TAG_VERSION;
    rec[0].a = KTRACE_VERSION;
    rec[1].tag = TAG_TICKS_PER_MS;
    rec[1].a = static_cast<uint32_t>(n);
    rec[1].b = static_cast<uint32_t>(n >> 32);
  }

  // Stash our static metadata
  ReportStaticNames();
  return ZX_OK;
}

ssize_t KTraceState::ReadUser(void* ptr, uint32_t off, size_t len) {
  Guard<Mutex> guard(&lock_);

  // If we were never configured to have a target buffer, our "docs" say that we
  // are supposed to return ZX_ERR_NOT_SUPPORTED.
  //
  // https://fuchsia.dev/fuchsia-src/reference/syscalls/ktrace_read
  if (!target_bufsize_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // We cannot read the buffer while it is in the started state.
  if (is_started_) {
    return ZX_ERR_BAD_STATE;
  }

  // If we are in the lock_, and we are stopped, then the group mask
  // must be 0, and we must have synchronized with any in-flight writes by now.
  DEBUG_ASSERT(grpmask_and_inflight_writes_.load(ktl::memory_order_acquire) == 0);

  // So, while it may appear like a bad thing to be holding the write lock for
  // this long, it really should not be much of an issue.  The grpmask is
  // disabled, so no new writes are coming in, and the lock_ is blocking any
  // other threads which might be attempting command and control operations.
  //
  // The only potential place where another thread might contend on the write
  // lock here would be if someone was trying to add a name record to the trace
  // with the |always| flag set (ignoring the grpmask).  This should only ever
  // happen during rewind and start operations which are serialized by
  // lock_.
  //
  // TL;DR - holding this lock at this point is only about making sure our
  // memory ordering semantics are correct (and we are seeing the proper values
  // for the last mutations performed by a writer outside of lock_), and about
  // making the static checker happy.  There should be no lock contention at
  // this point.
  Guard<SpinLock, IrqSave> write_guard{&write_lock_};

  // The amount of data we have to exfiltrate is equal to the distance between
  // the read and the write pointers, plus the non-circular region of the buffer
  // (if we are in circular mode).
  const uint32_t avail = [this]() TA_REQ(write_lock_) -> uint32_t {
    if (circular_size_ == 0) {
      DEBUG_ASSERT(rd_ == 0);
      DEBUG_ASSERT(wr_ <= bufsize_);
      return static_cast<uint32_t>(wr_);
    } else {
      DEBUG_ASSERT(rd_ <= wr_);
      DEBUG_ASSERT((wr_ - rd_) <= circular_size_);
      return static_cast<uint32_t>(wr_ - rd_) + wrap_offset_;
    }
  }();

  // null read is a query for trace buffer size
  //
  // TODO(johngro):  What are we supposed to return here?  The total number of
  // available bytes, or the total number of bytes which would have been
  // available had we started reading from |off|?  Our "docs" say nothing about
  // this.  For now, I'm just going to maintain the existing behavior and return
  // all of the available bytes, but someday the defined behavior of this API
  // needs to be clearly specified.
  if (ptr == nullptr) {
    return avail;
  }

  // constrain read to available buffer
  if (off >= avail) {
    return 0;
  }
  len = ktl::min<size_t>(len, avail - off);

  // Go ahead and copy the data.
  if (circular_size_ == 0) {
    // Non-circular mode is simple.
    if (CopyToUser(ptr, buffer_ + off, len) != ZX_OK) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    // circular mode requires a bit more care.
    size_t done = 0;
    size_t remaining = len;
    uint8_t* const ptr8 = reinterpret_cast<uint8_t*>(ptr);
    DEBUG_ASSERT(len >= done);

    // Start by consuming the non-circular portion of the buffer, taking into
    // account the offset.
    if (off < wrap_offset_) {
      const size_t todo = ktl::min<size_t>(wrap_offset_ - off, remaining);

      if (CopyToUser(ptr8, buffer_ + off, todo) != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }

      done = todo;
      remaining -= todo;
      off = 0;
    } else {
      off -= wrap_offset_;
    }

    // Now consume as much of the circular payload as we have space for.
    if (remaining) {
      const uint32_t rd_offset = PtrToCircularOffset(rd_ + off);
      DEBUG_ASSERT(rd_offset <= bufsize_);
      size_t todo = ktl::min<size_t>(bufsize_ - rd_offset, remaining);

      if (CopyToUser(ptr8 + done, buffer_ + rd_offset, todo) != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }

      done += todo;
      remaining -= todo;

      if (remaining) {
        DEBUG_ASSERT(remaining <= (rd_offset - wrap_offset_));
        DEBUG_ASSERT(done + remaining == len);
        if (CopyToUser(ptr8 + done, buffer_ + wrap_offset_, remaining) != ZX_OK) {
          return ZX_ERR_INVALID_ARGS;
        }
      }
    }
  }

  // Success!
  return len;
}

// Write out a ktrace record with no payload.
template <>
void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts) {
  DEBUG_ASSERT(KTRACE_LEN(effective_tag) >= sizeof(ktrace_header_t));

  AutoWriteInFlight inflight_manager(*this);
  if (unlikely(!tag_enabled(effective_tag, inflight_manager.observed_grpmask()))) {
    return;
  }

  if (explicit_ts == kRecordCurrentTimestamp) {
    explicit_ts = ktrace_timestamp();
  }

  if (PendingCommit reservation = Reserve(effective_tag); reservation.is_valid()) {
    reservation.hdr()->ts = explicit_ts;
    reservation.hdr()->tid = MakeTidField(effective_tag);
  } else {
    DisableGroupMask();
  }
}

// Write out a ktrace record with the given arguments as a payload.
//
// Arguments must be of the same type.
template <typename... Args>
void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, Args... args) {
  DEBUG_ASSERT(KTRACE_LEN(effective_tag) >= (sizeof(ktrace_header_t) + sizeof...(Args)));

  AutoWriteInFlight inflight_manager(*this);
  if (unlikely(!tag_enabled(effective_tag, inflight_manager.observed_grpmask()))) {
    return;
  }

  if (explicit_ts == kRecordCurrentTimestamp) {
    explicit_ts = ktrace_timestamp();
  }

  if (PendingCommit reservation = Reserve(effective_tag); reservation.is_valid()) {
    // Fill out most of the header.  Do not commit the tag until we have the
    // entire record written.
    reservation.hdr()->ts = explicit_ts;
    reservation.hdr()->tid = MakeTidField(effective_tag);

    // Fill out the payload.
    auto payload_src = {args...};
    using PayloadType = typename decltype(payload_src)::value_type;
    auto payload_tgt = reinterpret_cast<PayloadType*>(reservation.hdr() + 1);
    uint32_t i = 0;
    for (auto arg : payload_src) {
      payload_tgt[i++] = arg;
    }
  } else {
    DisableGroupMask();
  }
}

void KTraceState::WriteRecordTiny(uint32_t tag, uint32_t arg) {
  AutoWriteInFlight inflight_manager(*this);
  if (unlikely(!tag_enabled(tag, inflight_manager.observed_grpmask()))) {
    return;
  }

  // Tiny records are always 16 bytes.
  tag = (tag & 0xFFFFFFF0) | 2;

  if (PendingCommit reservation = Reserve(tag); reservation.is_valid()) {
    reservation.hdr()->ts = ktrace_timestamp();
    reservation.hdr()->tid = arg;
  } else {
    DisableGroupMask();
  }
}

void KTraceState::WriteNameEtc(uint32_t tag, uint32_t id, uint32_t arg, const char* name,
                               bool always) {
  auto ShouldTrace = [tag, always](uint32_t mask) -> bool {
    return tag_enabled(tag, mask) || always;
  };

  if (ShouldTrace(grpmask())) {
    AutoWriteInFlight in_flight_manager(*this);
    if (unlikely(!ShouldTrace(in_flight_manager.observed_grpmask()))) {
      return;
    }

    const uint32_t len = static_cast<uint32_t>(strnlen(name, ZX_MAX_NAME_LEN - 1));

    // set size to: sizeof(hdr) + len + 1, round up to multiple of 8
    tag = (tag & 0xFFFFFFF0) | ((KTRACE_NAMESIZE + len + 1 + 7) >> 3);

    if (PendingCommit reservation = Reserve(tag); reservation.is_valid()) {
      ktrace_rec_name_t* rec = reinterpret_cast<ktrace_rec_name_t*>(reservation.hdr());
      rec->id = id;
      rec->arg = arg;
      memcpy(rec->name, name, len);
      rec->name[len] = 0;
    } else {
      DisableGroupMask();
    }
  }
}

void KTraceState::ReportStaticNames() {
  ktrace_report_syscalls();
  ktrace_report_probes();
  ktrace_report_vcpu_meta();
}

void KTraceState::ReportThreadProcessNames() {
  ktrace_report_live_processes();
  ktrace_report_live_threads();
}

zx_status_t KTraceState::AllocBuffer() {
  // The buffer is allocated once, then never deleted.  If it has already been
  // allocated, then we are done.
  {
    Guard<SpinLock, IrqSave> write_guard{&write_lock_};
    if (buffer_) {
      return ZX_OK;
    }
  }

  // We require that our buffer be a multiple of page size, and non-zero.  If
  // the target buffer size ends up being zero, it is most likely because boot
  // args set the buffer size to zero.  For now, report NOT_SUPPORTED up the
  // stack to signal to usermode tracing (hitting AllocBuffer via Start) that
  // ktracing has been disabled.
  //
  // TODO(johngro): Do this rounding in Init
  target_bufsize_ = static_cast<uint32_t>(target_bufsize_ & ~(PAGE_SIZE - 1));
  if (!target_bufsize_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  DEBUG_ASSERT(is_started_ == false);

  zx_status_t status;
  VmAspace* aspace = VmAspace::kernel_aspace();
  void* ptr;
  if ((status = aspace->Alloc("ktrace", target_bufsize_, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                              ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE)) < 0) {
    DiagsPrintf(INFO, "ktrace: cannot alloc buffer %d\n", status);
    return ZX_ERR_NO_MEMORY;
  }

  {
    Guard<SpinLock, IrqSave> write_guard{&write_lock_};
    buffer_ = static_cast<uint8_t*>(ptr);
    bufsize_ = target_bufsize_;
  }
  DiagsPrintf(INFO, "ktrace: buffer at %p (%u bytes)\n", ptr, target_bufsize_);

  // Rewind will take care of writing the metadata records in addition to the
  // static name records as it resets the state.
  [[maybe_unused]] zx_status_t rewind_res = RewindLocked();
  DEBUG_ASSERT(rewind_res == ZX_OK);

  return ZX_OK;
}

KTraceState::PendingCommit KTraceState::Reserve(uint32_t tag) {
  constexpr uint32_t kUncommitedRecordTag = 0;
  auto Commit = [](void* ptr, uint32_t tag) -> void {
    ktl::atomic_ref(*static_cast<uint32_t*>(ptr)).store(tag, ktl::memory_order_release);
  };

  const uint32_t amt = KTRACE_LEN(tag);
  DEBUG_ASSERT(amt >= sizeof(uint32_t));

  Guard<SpinLock, IrqSave> write_guard{&write_lock_};
  if (!bufsize_) {
    return nullptr;
  }

  if (circular_size_ == 0) {
    DEBUG_ASSERT(bufsize_ >= wr_);
    const size_t space = bufsize_ - wr_;

    // if there is not enough space, we are done.
    if (space < amt) {
      return nullptr;
    }

    // We have the space for this record.  Stash the tag with a sentinel value
    // of zero, indicating that there is a reservation here, but that the record
    // payload has not been fully committed yet.
    void* ptr = buffer_ + wr_;
    Commit(ptr, kUncommitedRecordTag);
    wr_ += amt;
    return {ptr, tag};
  } else {
    // If there is not enough space in this circular buffer to hold our message,
    // don't even try.  Just give up.
    if (amt > circular_size_) {
      return nullptr;
    }

    while (true) {
      // Start by figuring out how much space we want to reserve.  Typically, we
      // will just reserve the space we need for our record. If, however, the
      // space at the end of the circular buffer is not enough to contiguously
      // hold our record, we reserve that amount of space instead, so that we
      // can put in a placeholder record at the end of the buffer which will be
      // skipped, in addition to our actual record.
      const uint32_t wr_offset = PtrToCircularOffset(wr_);
      const uint32_t contiguous_space = bufsize_ - wr_offset;
      const uint32_t to_reserve = ktl::min(contiguous_space, amt);
      DEBUG_ASSERT((to_reserve > 0) && ((to_reserve & 0x7) == 0));

      // Do we have the space for our reservation?  If not, then
      // move the read pointer forward until we do.
      DEBUG_ASSERT((wr_ >= rd_) && ((wr_ - rd_) <= circular_size_));
      size_t avail = circular_size_ - (wr_ - rd_);
      while (avail < to_reserve) {
        // We have to have space for a header tag.
        const uint32_t rd_offset = PtrToCircularOffset(rd_);
        DEBUG_ASSERT(bufsize_ - rd_offset >= sizeof(uint32_t));

        // Make sure that we read the next tag in the sequence with acquire
        // semantics.  Before committing, records which have been reserved in
        // the trace buffer will have their tag set to zero inside of the write
        // lock. During commit, however, the actual record tag (with non-zero
        // length) will be written to memory atomically with release semantics,
        // outside of the lock.
        uint32_t* rd_tag_ptr = reinterpret_cast<uint32_t*>(buffer_ + rd_offset);
        const uint32_t rd_tag =
            ktl::atomic_ref<uint32_t>(*rd_tag_ptr).load(ktl::memory_order_acquire);
        const uint32_t sz = KTRACE_LEN(rd_tag);

        // If our size is 0, it implies that we managed to wrap around and catch
        // and catch the read pointer when it is pointing to a still uncommitted
        // record.  We are not in a position where we can wait.  Simply fail the
        // reservation.
        if (sz == 0) {
          return nullptr;
        }

        // Now go ahead and move read up.
        rd_ += sz;
        avail += sz;
      }

      // Great, we now have space for our reservation.  If we have enough space
      // for our entire record, go ahead and reserve the space now.  Otherwise,
      // stuff in a placeholder which fills all of the remaining contiguous
      // space in the buffer, then try the allocation again.
      void* ptr = buffer_ + wr_offset;
      wr_ += to_reserve;
      if (amt == to_reserve) {
        Commit(ptr, kUncommitedRecordTag);
        return {ptr, tag};
      } else {
        DEBUG_ASSERT(amt > to_reserve);
        Commit(ptr, KTRACE_TAG(0u, 0u, to_reserve));
      }
    }
  }
}

// Instantiate used versions of |KTraceState::WriteRecord|.
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a);
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a,
                                       uint32_t b);
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a,
                                       uint32_t b, uint32_t c);
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a,
                                       uint32_t b, uint32_t c, uint32_t d);
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint64_t a);
template void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, uint64_t a,
                                       uint64_t b);

}  // namespace internal

zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr) {
  using StartMode = ::internal::KTraceState::StartMode;
  switch (action) {
    case KTRACE_ACTION_START:
    case KTRACE_ACTION_START_CIRCULAR: {
      StartMode start_mode =
          (action == KTRACE_ACTION_START) ? StartMode::Saturate : StartMode::Circular;

      zx_status_t res = KTRACE_STATE.Start(options ? options : KTRACE_GRP_ALL, start_mode);
      if (res == ZX_OK) {
        ktrace_probe(TraceAlways, TraceContext::Thread, "ktrace_ready"_stringref);
      }

      return res;
    }

    case KTRACE_ACTION_STOP:
      return KTRACE_STATE.Stop();

    case KTRACE_ACTION_REWIND:
      return KTRACE_STATE.Rewind();

    case KTRACE_ACTION_NEW_PROBE: {
      const char* const string_in = static_cast<const char*>(ptr);

      StringRef* ref = ktrace_find_probe(string_in);
      if (ref != nullptr) {
        return ref->id;
      }

      struct DynamicStringRef {
        DynamicStringRef(const char* string) : string_ref{storage} {
          memcpy(storage, string, sizeof(storage));
        }

        StringRef string_ref;
        char storage[ZX_MAX_NAME_LEN];
      };

      // TODO(eieio,dje): Figure out how to constrain this to prevent abuse by
      // creating huge numbers of unique probes.
      fbl::AllocChecker alloc_checker;
      DynamicStringRef* dynamic_ref = new (&alloc_checker) DynamicStringRef{string_in};
      if (!alloc_checker.check()) {
        return ZX_ERR_NO_MEMORY;
      }

      ktrace_add_probe(&dynamic_ref->string_ref);
      return dynamic_ref->string_ref.id;
    }

    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void ktrace_init(unsigned level) {
  // There's no utility in setting up the singleton ktrace instance if there are
  // no syscalls to access it. See zircon/kernel/syscalls/debug.cc for the
  // corresponding syscalls. Note that because KTRACE_STATE grpmask starts at 0
  // and will not be changed, the other functions in this file need not check
  // for enabled-ness manually.
  const bool syscalls_enabled = gBootOptions->enable_debugging_syscalls;
  const uint32_t bufsize = syscalls_enabled ? (gBootOptions->ktrace_bufsize << 20) : 0;
  const uint32_t initial_grpmask = gBootOptions->ktrace_grpmask;

  if (!bufsize) {
    dprintf(INFO, "ktrace: disabled\n");
    return;
  }

  KTRACE_STATE.Init(bufsize, initial_grpmask);

  if (!initial_grpmask) {
    dprintf(INFO, "ktrace: delaying buffer allocation\n");
  }
}

// Finish initialization before starting userspace (i.e. before debug syscalls can occur).
LK_INIT_HOOK(ktrace, ktrace_init, LK_INIT_LEVEL_USER - 1)
