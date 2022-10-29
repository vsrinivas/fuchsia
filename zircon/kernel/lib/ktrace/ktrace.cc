// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/boot-options/boot-options.h>
#include <lib/fxt/fields.h>
#include <lib/ktrace.h>
#include <lib/ktrace/ktrace_internal.h>
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

#include <ktl/enforce.h>

// The global ktrace state.
internal::KTraceState KTRACE_STATE;

namespace {

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
    // Also emit an FXT string record.
    // TEMPORARY(fxbug.dev/98176): Since ktrace_provider also creates its own
    // string references, use the upper half of the index space.
    const uint16_t fxt_id = static_cast<uint16_t>(ref->id) | 0x4000;
    fxt_string_record(fxt_id, ref->string, strnlen(ref->string, ZX_MAX_NAME_LEN - 1));
  }
}

// TODO(fxbug.dev/112751)
void ktrace_report_cpu_pseudo_threads() {
  const uint max_cpus = arch_max_num_cpus();
  char name[32];
  for (uint i = 0; i < max_cpus; i++) {
    snprintf(name, sizeof(name), "cpu-%u", i);
    fxt_kernel_object(TAG_THREAD_NAME, /* always */ true, kKernelPseudoCpuBase + i,
                      ZX_OBJ_TYPE_THREAD, fxt::StringRef(name),
                      fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId>(
                          fxt::StringRef("process"_stringref->GetFxtId()), kNoProcess));
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

  StringRef::PreRegister();

  if (initial_groups != 0) {
    if (AllocBuffer() == ZX_OK) {
      ReportStaticNames();
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
    ReportStaticNames();
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

  return ZX_OK;
}

ssize_t KTraceState::ReadUser(user_out_ptr<void> ptr, uint32_t off, size_t len) {
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

  // Grab the write lock, then figure out what we need to copy.  We need to make
  // sure to drop the lock before calling CopyToUser (holding spinlocks while
  // copying to user mode memory is not allowed because of the possibility of
  // faulting).
  //
  // It may appear like a bad thing to drop the lock before performing the copy,
  // but it really should not be much of an issue.  The grpmask is disabled, so
  // no new writes are coming in, and the lock_ is blocking any other threads
  // which might be attempting command and control operations.
  //
  // The only potential place where another thread might contend on the write
  // lock here would be if someone was trying to add a name record to the trace
  // with the |always| flag set (ignoring the grpmask).  This should only ever
  // happen during rewind and start operations which are serialized by lock_.
  struct Region {
    uint8_t* ptr{nullptr};
    size_t len{0};
  };

  struct ToCopy {
    size_t avail{0};
    Region regions[3];
  } to_copy = [this, &off, &len]() -> ToCopy {
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

    // constrain read to available buffer
    if (off >= avail) {
      return ToCopy{};
    }

    len = ktl::min<size_t>(len, avail - off);

    ToCopy ret{.avail = avail};
    uint32_t ndx = 0;

    // Go ahead report the region(s) of data which need to be copied.
    if (circular_size_ == 0) {
      // Non-circular mode is simple.
      DEBUG_ASSERT(ndx < ktl::size(ret.regions));
      Region& r = ret.regions[ndx++];
      r.ptr = buffer_ + off;
      r.len = len;
    } else {
      // circular mode requires a bit more care.
      size_t remaining = len;

      // Start by consuming the non-circular portion of the buffer, taking into
      // account the offset.
      if (off < wrap_offset_) {
        const size_t todo = ktl::min<size_t>(wrap_offset_ - off, remaining);

        DEBUG_ASSERT(ndx < ktl::size(ret.regions));
        Region& r = ret.regions[ndx++];
        r.ptr = buffer_ + off;
        r.len = todo;

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

        {
          DEBUG_ASSERT(ndx < ktl::size(ret.regions));
          Region& r = ret.regions[ndx++];
          r.ptr = buffer_ + rd_offset;
          r.len = todo;
        }

        remaining -= todo;

        if (remaining) {
          DEBUG_ASSERT(remaining <= (rd_offset - wrap_offset_));
          DEBUG_ASSERT(ndx < ktl::size(ret.regions));
          Region& r = ret.regions[ndx++];
          r.ptr = buffer_ + wrap_offset_;
          r.len = remaining;
        }
      }
    }

    return ret;
  }();

  // null read is a query for trace buffer size
  //
  // TODO(johngro):  What are we supposed to return here?  The total number of
  // available bytes, or the total number of bytes which would have been
  // available had we started reading from |off|?  Our "docs" say nothing about
  // this.  For now, I'm just going to maintain the existing behavior and return
  // all of the available bytes, but someday the defined behavior of this API
  // needs to be clearly specified.
  if (!ptr) {
    return to_copy.avail;
  }

  // constrain read to available buffer
  if (off >= to_copy.avail) {
    return 0;
  }

  // Go ahead and copy the data.
  auto ptr8 = ptr.reinterpret<uint8_t>();
  size_t done = 0;
  for (const auto& r : to_copy.regions) {
    if (r.ptr != nullptr) {
      zx_status_t copy_result = ZX_OK;
      // Performing user copies whilst holding locks is not generally allowed, however in this case
      // the entire purpose of lock_ is to serialize these operations and so is safe to be held for
      // this copy.
      //
      // TOOD(fxb/101783): Determine if this should be changed to capture faults and resolve them
      // outside the lock.
      guard.CallUntracked([&] { copy_result = CopyToUser(ptr8.byte_offset(done), r.ptr, r.len); });
      if (copy_result != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
    }

    done += r.len;
  }

  // Success!
  return done;
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
  ktrace_report_probes();
  ktrace_report_cpu_pseudo_threads();
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

  // Rewind will take care of writing the metadata records as it resets the state.
  [[maybe_unused]] zx_status_t rewind_res = RewindLocked();
  DEBUG_ASSERT(rewind_res == ZX_OK);

  return ZX_OK;
}

KTraceState::PendingCommit KTraceState::Reserve(uint32_t tag) {
  void* ptr = ReserveRaw(KTRACE_LEN(tag));
  if (ptr != nullptr) {
    return {ptr, tag};
  } else {
    return nullptr;
  }
}

void* KTraceState::ReserveRaw(uint32_t num_bytes) {
  constexpr uint32_t kUncommitedRecordTag = 0;
  auto Commit = [](void* ptr, uint32_t tag) -> void {
    ktl::atomic_ref(*static_cast<uint32_t*>(ptr)).store(tag, ktl::memory_order_release);
  };

  DEBUG_ASSERT(num_bytes >= sizeof(uint32_t));
  DEBUG_ASSERT(num_bytes % sizeof(uint64_t) == 0);

  Guard<SpinLock, IrqSave> write_guard{&write_lock_};
  if (!bufsize_) {
    return nullptr;
  }

  if (circular_size_ == 0) {
    DEBUG_ASSERT(bufsize_ >= wr_);
    const size_t space = bufsize_ - wr_;

    // if there is not enough space, we are done.
    if (space < num_bytes) {
      return nullptr;
    }

    // We have the space for this record.  Stash the tag with a sentinel value
    // of zero, indicating that there is a reservation here, but that the record
    // payload has not been fully committed yet.
    void* ptr = buffer_ + wr_;
    Commit(ptr, kUncommitedRecordTag);
    wr_ += num_bytes;
    return ptr;
  } else {
    // If there is not enough space in this circular buffer to hold our message,
    // don't even try.  Just give up.
    if (num_bytes > circular_size_) {
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
      const uint32_t to_reserve = ktl::min(contiguous_space, num_bytes);
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
      if (num_bytes == to_reserve) {
        Commit(ptr, kUncommitedRecordTag);
        return ptr;
      } else {
        DEBUG_ASSERT(num_bytes > to_reserve);
        Commit(ptr, KTRACE_TAG(0u, 0u, to_reserve));
      }
    }
  }
}

zx::result<KTraceState::FxtCompatWriter::Reservation> KTraceState::FxtCompatWriter::Reserve(
    uint64_t header) {
  // Combine the record size from the provided FXT header with the rest of the
  // KTrace tag.
  uint32_t fxt_words = fxt::RecordFields::RecordSize::Get<uint32_t>(header);

  // KTrace size field is 4 bits, making a maximum of 15 words, and one word is
  // used for the KTrace header, so we can only fit a maximum of 14 words of
  // FXT.
  if (fxt_words > 14) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  void* ptr = ks_.ReserveRaw((fxt_words + 1) * sizeof(uint64_t));
  if (ptr == nullptr) {
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  // Combine the size from the FXT header with the rest of the previously
  // provided ktrace header. Additionally, set KTRACE_GRP_FXT bit.
  uint64_t ktrace_header = (tag_ & ~0xF) | (fxt_words + 1) | KTRACE_GRP_TO_MASK(KTRACE_GRP_FXT);

  KTraceState::FxtCompatWriter::Reservation reservation{reinterpret_cast<uint64_t*>(ptr),
                                                        ktrace_header};
  // Immediately write the FXT header. The KTrace header will be written on
  // commit to finalize the record.
  reservation.WriteWord(header);

  return zx::ok(reservation);
}

void KTraceState::FxtCompatWriter::Reservation::WriteWord(uint64_t word) {
  DEBUG_ASSERT(word_offset_ < (ktrace_header_ & 0xF));
  *(ptr_ + word_offset_) = word;
  word_offset_++;
}

void KTraceState::FxtCompatWriter::Reservation::WriteBytes(const void* bytes, size_t num_bytes) {
  size_t num_words = (num_bytes + 7) / 8;
  DEBUG_ASSERT(word_offset_ + num_words - 1 < (ktrace_header_ & 0xF));
  // Write 0 to the last word to cover any padding bytes.
  *(ptr_ + (word_offset_ + num_words - 1)) = 0;
  memcpy(static_cast<void*>(ptr_ + word_offset_), bytes, num_bytes);
  word_offset_ += num_words;
}

void KTraceState::FxtCompatWriter::Reservation::Commit() {
  ktl::atomic_ref(*ptr_).store(ktrace_header_, ktl::memory_order_release);
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
        explicit DynamicStringRef(const char* string) { memcpy(storage, string, sizeof(storage)); }

        char storage[ZX_MAX_NAME_LEN];
        StringRef string_ref{storage};
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

static void ktrace_init(unsigned level) {
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
