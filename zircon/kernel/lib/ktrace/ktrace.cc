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

  target_bufsize_ = target_bufsize;

  if (initial_groups != 0) {
    if (AllocBuffer() == ZX_OK) {
      ReportThreadProcessNames();
      is_started_ = true;
    }
  }

  grpmask_.store(KTRACE_GRP_TO_MASK(initial_groups));
}

zx_status_t KTraceState::Start(uint32_t groups) {
  Guard<Mutex> guard(&lock_);

  if (groups == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (zx_status_t status = AllocBuffer(); status != ZX_OK) {
    return status;
  }

  // If we are not yet started, we need to report the current thread and process
  // names, and update our and buffer bookkeeping. If we are already started,
  // then this request to start should be treated simply as an update of the
  // group mask.
  if (!is_started_) {
    marker_ = 0;
    ReportThreadProcessNames();
    is_started_ = true;
  }

  grpmask_.store(KTRACE_GRP_TO_MASK(groups));

  return ZX_OK;
}

zx_status_t KTraceState::Stop() {
  Guard<Mutex> guard(&lock_);

  // Stopping an already stopped KTrace is OK, the operation is idempotent.
  if (!is_started_) {
    return ZX_OK;
  }

  grpmask_.store(0);
  uint32_t n = offset_.load();
  if (n > bufsize_) {
    marker_ = bufsize_;
  } else {
    marker_ = n;
  }

  is_started_ = false;

  return ZX_OK;
}

zx_status_t KTraceState::RewindLocked() {
  if (is_started_) {
    return ZX_ERR_BAD_STATE;
  }

  // roll back to just after the metadata
  offset_.store(KTRACE_RECSIZE * 2);
  buffer_full_.store(false);
  ReportStaticNames();

  return ZX_OK;
}

ssize_t KTraceState::ReadUser(void* ptr, uint32_t off, size_t len) {
  Guard<Mutex> guard(&lock_);

  if (is_started_) {
    return ZX_ERR_BAD_STATE;
  }

  // Buffer size is limited by the marker if set,
  // otherwise limited by offset (last written point).
  // Offset can end up pointing past the end, so clip
  // it to the actual buffer size to be safe.
  uint32_t max;
  if (marker_) {
    max = marker_;
  } else {
    max = offset_.load();
    if (max > bufsize_) {
      max = bufsize_;
    }
  }

  // null read is a query for trace buffer size
  if (ptr == nullptr) {
    return max;
  }

  // constrain read to available buffer
  if (off >= max) {
    return 0;
  }
  if (len > (max - off)) {
    len = max - off;
  }

  if (arch_copy_to_user(ptr, buffer_ + off, len) != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }

  return len;
}

// Write out a ktrace record with no payload.
template <>
void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts) {
  DEBUG_ASSERT(KTRACE_LEN(effective_tag) >= sizeof(ktrace_header_t));

  if (explicit_ts == kRecordCurrentTimestamp) {
    explicit_ts = ktrace_timestamp();
  }

  Open(effective_tag, explicit_ts);
}

// Write out a ktrace record with the given arguments as a payload.
//
// Arguments must be of the same type.
template <typename... Args>
void KTraceState::WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, Args... args) {
  DEBUG_ASSERT(KTRACE_LEN(effective_tag) >= (sizeof(ktrace_header_t) + sizeof...(Args)));

  if (explicit_ts == kRecordCurrentTimestamp) {
    explicit_ts = ktrace_timestamp();
  }

  // Write out each arg.
  auto payload = {args...};

  using PayloadType = typename decltype(payload)::value_type;
  if (auto* data = static_cast<PayloadType*>(Open(effective_tag, explicit_ts)); data != nullptr) {
    int i = 0;
    for (auto arg : payload) {
      data[i++] = arg;
    }
  }
}

void KTraceState::WriteRecordTiny(uint32_t tag, uint32_t arg) {
  tag = (tag & 0xFFFFFFF0) | 2;
  uint32_t off;

  if ((off = (offset_.fetch_add(KTRACE_HDRSIZE))) >= (bufsize_)) {
    // if we arrive at the end, stop
    Disable();
  } else {
    ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(buffer_ + off);
    hdr->ts = ktrace_timestamp();
    hdr->tag = tag;
    hdr->tid = arg;
  }
}

void KTraceState::WriteNameEtc(uint32_t tag, uint32_t id, uint32_t arg, const char* name,
                               bool always) {
  if (tag_enabled(tag) || (always && !buffer_full_.load())) {
    const uint32_t len = static_cast<uint32_t>(strnlen(name, ZX_MAX_NAME_LEN - 1));

    // set size to: sizeof(hdr) + len + 1, round up to multiple of 8
    tag = (tag & 0xFFFFFFF0) | ((KTRACE_NAMESIZE + len + 1 + 7) >> 3);

    uint32_t off;
    if ((off = offset_.fetch_add(KTRACE_LEN(tag))) >= bufsize_) {
      // if we arrive at the end, stop
      Disable();
    } else {
      ktrace_rec_name_t* rec = reinterpret_cast<ktrace_rec_name_t*>(buffer_ + off);
      rec->tag = tag;
      rec->id = id;
      rec->arg = arg;
      memcpy(rec->name, name, len);
      rec->name[len] = 0;
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
  if (buffer_) {
    return ZX_OK;
  }

  // We require that our buffer be a multiple of page size, and non-zero.  If
  // the target buffer size ends up being zero, it is most likely because boot
  // args set the buffer size to zero.  For now, report NOT_SUPPORTED up the
  // stack to signal to usermode tracing (hitting AllocBuffer via Start) that
  // ktracing has been disabled.
  target_bufsize_ = static_cast<uint32_t>(target_bufsize_ & ~(PAGE_SIZE - 1));
  if (!target_bufsize_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  DEBUG_ASSERT(is_started_ == false);

  zx_status_t status;
  VmAspace* aspace = VmAspace::kernel_aspace();
  if ((status = aspace->Alloc("ktrace", target_bufsize_, reinterpret_cast<void**>(&buffer_), 0,
                              VmAspace::VMM_FLAG_COMMIT,
                              ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE)) < 0) {
    DiagsPrintf(INFO, "ktrace: cannot alloc buffer %d\n", status);
    return ZX_ERR_NO_MEMORY;
  }

  // The last packet written can overhang the end of the buffer,
  // so we reduce the reported size by the max size of a record
  bufsize_ = target_bufsize_ - 256;
  DiagsPrintf(INFO, "ktrace: buffer at %p (%u bytes)\n", buffer_, target_bufsize_);

  // write metadata to the first two event slots
  uint64_t n = ktrace_ticks_per_ms();
  ktrace_rec_32b_t* rec = reinterpret_cast<ktrace_rec_32b_t*>(buffer_);
  rec[0].tag = TAG_VERSION;
  rec[0].a = KTRACE_VERSION;
  rec[1].tag = TAG_TICKS_PER_MS;
  rec[1].a = static_cast<uint32_t>(n);
  rec[1].b = static_cast<uint32_t>(n >> 32);

  [[maybe_unused]] zx_status_t rewind_res = RewindLocked();
  DEBUG_ASSERT(rewind_res == ZX_OK);

  // Report an event for "tracing is all set up now".  This also
  // serves to ensure that there will be at least one static probe
  // entry so that the __{start,stop}_ktrace_probe symbols above
  // will be defined by the linker.
  ktrace_probe(TraceAlways, TraceContext::Thread, "ktrace_ready"_stringref);

  return ZX_OK;
}

void* KTraceState::Open(uint32_t tag, uint64_t ts) {
  uint32_t off;
  if ((off = offset_.fetch_add(KTRACE_LEN(tag))) >= bufsize_) {
    // if we arrive at the end, stop
    Disable();
    return nullptr;
  }

  ktrace_header_t* hdr = reinterpret_cast<ktrace_header_t*>(buffer_ + off);
  hdr->ts = ts;
  hdr->tag = tag;
  hdr->tid = KTRACE_FLAGS(tag) & KTRACE_FLAGS_CPU
                 ? arch_curr_cpu_num()
                 : static_cast<uint32_t>(Thread::Current::Get()->tid());
  return hdr + 1;
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
  switch (action) {
    case KTRACE_ACTION_START:
      return KTRACE_STATE.Start(options ? options : KTRACE_GRP_ALL);

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
