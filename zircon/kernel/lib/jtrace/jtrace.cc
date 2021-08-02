// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/console.h>
#include <lib/io.h>
#include <lib/jtrace/jtrace.h>

#include "jtrace_internal.h"

namespace jtrace {
namespace {

// Make an attempt to validate a virtual address as being a valid kernel virtual
// address.  Do not allow this to be called when blocking is not allowed
// (holding a spinlock, hard IRQ time, etc) as blocking mutexes in the VM
// subsystem will need to be acquired in order to perform the validation.
bool ValidateVaddr(const void* val) {
  vaddr_t addr = reinterpret_cast<vaddr_t>(val);

  if (!is_kernel_address(addr)) {
    return false;
  }

  VmAspace* aspace = VmAspace::kernel_aspace();
  paddr_t pa;
  uint flags;
  zx_status_t err = aspace->arch_aspace().Query(addr, &pa, &flags);
  return (err == ZX_OK);
}

// SafeString is a small helper class which does its best to validate that
// string literal pointers recovered from a persistent trace buffer are valid
// before attempting to render them.  Persistent trace buffers are stored in
// "persistent" RAM passed to the kernel by the bootloader, and _could_ have
// suffered from corruption during a spontaneous reboot, so it is important to
// ensure that they represent a valid kernel virtual address before attempting
// to render them.
class SafeString {
 public:
  SafeString(const char* str, TraceBufferType buf_type) : str_(str) {
    // Only attempt to validate the string's virtual address if we are
    // attempting to print a recovered log.  If we are in the process of dumping
    // the current log, there is a very good chance that we are in the middle of
    // a panic and unable to validate virtual addresses due to the VM locking
    // requirements.
    if ((buf_type == TraceBufferType::Recovered) && !ValidateVaddr(str_)) {
      snprintf(replacement_buf_, sizeof(replacement_buf_), "<Invalid %p>", str_);
      str_ = replacement_buf_;
    }
  }

  // No copy, no move.
  SafeString(const SafeString&) = delete;
  SafeString& operator=(const SafeString&) = delete;
  SafeString(SafeString&&) = delete;
  SafeString& operator=(SafeString&&) = delete;

  const char* get() const { return str_; }

 private:
  const char* str_;
  char replacement_buf_[32];
};

class ProductionTraceHooks final : public TraceHooks {
 public:
  void PrintWarning(const char* fmt, ...) final __PRINTFLIKE(2, 3) {
    va_list args;
    va_start(args, fmt);
    InternalVPrintf(fmt, args);
    va_end(args);
  }

  void PrintInfo(const char* fmt, ...) final __PRINTFLIKE(2, 3) {
    va_list args;
    va_start(args, fmt);
    InternalVPrintf(fmt, args);
    va_end(args);
  }

  void Hexdump(const void* data, size_t size) final { hexdump8(data, size); }

  void PrintTraceEntry(const Entry<UseLargeEntries::Yes>& e, TraceBufferType buf_type, zx_time_t ts,
                       zx_duration_t delta = 0) final {
    InternalPrintTraceEntry(e, buf_type, ts, delta);
  }

  void PrintTraceEntry(const Entry<UseLargeEntries::No>& e, TraceBufferType buf_type, zx_time_t ts,
                       zx_duration_t delta = 0) final {
    InternalPrintTraceEntry(e, buf_type, ts, delta);
  }

 private:
  template <UseLargeEntries kUseLargeEntries>
  void InternalPrintTraceEntry(const Entry<kUseLargeEntries>& e, TraceBufferType buf_type,
                               zx_time_t ts, zx_duration_t delta) {
    const int64_t ts_sec = ts / ZX_SEC(1);
    const int64_t ts_nsec = ts % ZX_SEC(1);

    SafeString tag(e.tag, buf_type);

    const int64_t delta_usec = delta / ZX_USEC(1);
    const int64_t delta_nsec = delta % ZX_USEC(1);

    if constexpr (kUseLargeEntries == UseLargeEntries::No) {
      PrintInfo("[%4ld.%09ld][cpu %u] : %08x : (%5ld.%03ld uSec) : (%s)\n", ts_sec, ts_nsec,
                e.cpu_id, e.a, delta_usec, delta_nsec, tag.get());
    } else {
      const internal::FileFuncLineInfo* ffl_info = e.ffl_info;
      if ((buf_type == TraceBufferType::Recovered) && !ValidateVaddr(&e.ffl_info)) {
        static constexpr internal::FileFuncLineInfo kFallback = {
            .file = "<bad FFL pointer>", .func = "<bad FFL pointer>", .line = 0};
        ffl_info = &kFallback;
      }

      SafeString file(ffl_info->file, buf_type);
      SafeString func(ffl_info->func, buf_type);
      PrintInfo(
          "[%4ld.%09ld][cpu %u tid %8lu] : %08x %08x %08x %08x %016lx %016lx : (%8ld.%03ld uSec) : "
          "%s:%s:%d (%s)\n",
          ts_sec, ts_nsec, e.cpu_id, e.tid, e.a, e.b, e.c, e.d, e.e, e.f, delta_usec, delta_nsec,
          TrimFilename(file.get()), func.get(), ffl_info->line, tag.get());
    }
  }

  // Note: we print to an internally held static buffer which we then send
  // directly to the unbuffered stdout in order to avoid needing to render into
  // our current thread's linebuffer. Thread linebuffers are too short to hold
  // all of a large entry on a single line, and instead of increasing the
  // linebuffer size for all of the threads in the system, we choose to render
  // to a single statically allocated line buffer instead.
  //
  // This also means that JTRACE buffer dump operations are not technically
  // threadsafe. This is by design for the following reasons:
  //
  // 1) Kernel stacks are small (8kb by default) and we don't want to be putting
  //    large buffers on the stack when we can avoid it.
  // 2) Dumping of a JTRACE buffer usually happens during a panic, and we would
  //    very much like to avoid making any attempt to obtain any locks during the
  //    dump operation.  For example, what would happen if we panic'ed during
  //    a trace dump operation which was holding a lock meant to serialize dump
  //    operations?
  // 3) The only other place (aside from a panic) where a trace buffer is dumped
  //    is from the kernel console. Kernel console commands are already serialized
  //    using the singleton CommandLock.
  // 4) If the worst happens, and the shared buffer does end up being used
  //    concurrently, the implementation renders into the buffer using
  //    vsnprintf, and then outputs the rendered line using the Write method of
  //    a FILE structure which takes a ktl::string_view as an argument.  String
  //    views contain an explicit length which the Write method makes use of,
  //    not relying on the presence of an embedded null in the string to be
  //    rendered. So, at worst, the output might end up garbled, but there
  //    should be no chance of running off the end of the buffer.
  //
  void InternalVPrintf(const char* fmt, va_list args) {
    int res = vsnprintf(linebuffer_, sizeof(linebuffer_), fmt, args);
    if (res >= 0) {
      gStdoutUnbuffered.Write({linebuffer_, static_cast<size_t>(res)});
    } else {
      printf("Failed to output JTRACE line! (res %d)\n", res);
    }
  }

  static constexpr const char* TrimFilename(ktl::string_view fname) {
    size_t pos = fname.find_last_of('/');
    return ((pos == fname.npos) ? fname : fname.substr(pos + 1)).data();
  }

  char linebuffer_[256];
};

}  // namespace
}  // namespace jtrace

#if JTRACE_TARGET_BUFFER_SIZE > 0
// Storage
//
namespace {
using JTraceConfig = ::jtrace::Config<kJTraceTargetBufferSize, kJTraceLastEntryStorage,
                                      kJTraceIsPersistent, kJTraceUseLargeEntries>;
using Entry = typename JTraceConfig::Entry;

template <typename Config, typename = void>
class NonPersistentBuffer {
 public:
  static ktl::span<uint8_t> get() { return {}; }
};

template <typename Config>
class NonPersistentBuffer<Config,
                          ktl::enable_if_t<Config::kIsPersistent == jtrace::IsPersistent::No>> {
 public:
  static ktl::span<uint8_t> get() { return {data}; }

 private:
  static inline uint8_t data[Config::kTargetBufferSize];
};

lazy_init::LazyInit<jtrace::ProductionTraceHooks> g_trace_hooks;
lazy_init::LazyInit<jtrace::JTrace<JTraceConfig>> g_trace;

}  // namespace

// Thunks
//
void jtrace_init() {
  // Note: jtrace_init is called very early on in boot, before global
  // constructors have been called. Do not add any behavior which depends on
  // global ctors at this point in the code.
  g_trace_hooks.Initialize();
  g_trace.Initialize(g_trace_hooks.Get());
  if constexpr (JTraceConfig::kIsPersistent == jtrace::IsPersistent::No) {
    g_trace->SetLocation(NonPersistentBuffer<JTraceConfig>::get());
  }
}

void jtrace_set_location(void* ptr, size_t len) {
  g_trace->SetLocation({static_cast<uint8_t*>(ptr), len});
}

void jtrace_invalidate(void) { g_trace->Invalidate(); }

void jtrace_log(Entry& e) { g_trace->Log(e); }

void jtrace_dump(jtrace::TraceBufferType which) {
  if (which == jtrace::TraceBufferType::Recovered) {
    g_trace->DumpRecovered();
  } else {
    g_trace->Dump();
  }
}

// CLI
//
static int cmd_jtrace(int argc, const cmd_args* argv, uint32_t flags) {
  auto usage = [program = argv[0].str]() -> int {
    printf("usage: %s [-r|-i]\n", program);
    printf("  -r : dump the recovered trace buffer instead of the current trace buffer.\n");
    printf("  -i : dump information about the current JTRACE configuration.\n");
    return -1;
  };

  switch (argc) {
    case 1:
      jtrace_dump(jtrace::TraceBufferType::Current);
      break;

    case 2:
      if (strcmp(argv[1].str, "-r") == 0) {
        jtrace_dump(jtrace::TraceBufferType::Recovered);
      } else if (strcmp(argv[1].str, "-i") == 0) {
        if constexpr (JTraceConfig::kTargetBufferSize == 0) {
          printf("Debug tracing is not enabled in this build.\n");
        } else {
          ktl::span<uint8_t> location = g_trace->GetLocation();
          printf("JTRACE configuration\n");
          printf("--------------------\n");
          printf("Requested Buffer Size  : %zu\n", JTraceConfig::kTargetBufferSize);
          printf("Allocated Buffer Size  : %zu\n", location.size());
          printf("Allocated Buffer Loc   : %p\n", location.data());
          printf("Per-CPU last entry cnt : %zu\n", JTraceConfig::kLastEntryStorage);
          printf("Large entries          : %s\n",
                 (JTraceConfig::kUseLargeEntries == ::jtrace::UseLargeEntries::Yes) ? "yes" : "no");
          printf("Persistent             : %s\n",
                 (JTraceConfig::kIsPersistent == ::jtrace::IsPersistent::Yes) ? "yes" : "no");

          if ((JTraceConfig::kLastEntryStorage > 0) &&
              (JTraceConfig::kLastEntryStorage != arch_max_num_cpus())) {
            printf(
                "\nWarning! Configured per-cpu last entry count (%zu) does not match target's "
                "number of CPUs (%u)\n",
                JTraceConfig::kLastEntryStorage, arch_max_num_cpus());
          }
        }
      } else {
        return usage();
      }
      break;

    default:
      return usage();
  }

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("jtrace", "dump the current or recovered jtrace", &cmd_jtrace)
STATIC_COMMAND_END(jtrace)
#endif
