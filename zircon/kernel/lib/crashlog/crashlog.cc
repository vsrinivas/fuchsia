// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/crashlog.h"

#include <ctype.h>
#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/debuglog.h>
#include <lib/io.h>
#include <lib/lockup_detector.h>
#include <lib/version.h>
#include <platform.h>
#include <stdio.h>
#include <string-file.h>
#include <string.h>
#include <zircon/boot/crash-reason.h>

#include <arch/regs.h>
#include <fbl/enum_bits.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <vm/pmm.h>
#include <vm/pmm_checker.h>
#include <vm/vm.h>

crashlog_t g_crashlog = {};

PanicBuffer panic_buffer;

FILE stdout_panic_buffer{[](void*, ktl::string_view str) {
                           panic_buffer.Append(str);
                           return gStdoutNoPersist.Write(str);
                         },
                         nullptr};

namespace {

DECLARE_SINGLETON_MUTEX(RecoveredCrashlogLock);
fbl::RefPtr<VmObject> recovered_crashlog TA_GUARDED(RecoveredCrashlogLock::Get());

enum class RenderRegion : uint32_t {
  // clang-format off
  None             = 0x00,
  Banner           = 0x01,
  DebugInfo        = 0x02,
  CriticalCounters = 0x04,
  PanicBuffer      = 0x08,
  Dlog             = 0x10,
  All              = 0xffffffff,
  // clang-format on
};
FBL_ENABLE_ENUM_BITS(RenderRegion)

RenderRegion MapReasonToRegions(zircon_crash_reason_t reason) {
  switch (reason) {
    case ZirconCrashReason::NoCrash:
      return RenderRegion::None;

    case ZirconCrashReason::Oom:
    case ZirconCrashReason::UserspaceRootJobTermination:
      return RenderRegion::Banner | RenderRegion::CriticalCounters | RenderRegion::Dlog;

    case ZirconCrashReason::Panic:
    case ZirconCrashReason::SoftwareWatchdog:
      return RenderRegion::Banner | RenderRegion::DebugInfo | RenderRegion::CriticalCounters |
             RenderRegion::PanicBuffer | RenderRegion::Dlog;

    default:
      return RenderRegion::Banner;
  }
}

}  // namespace

size_t crashlog_to_string(ktl::span<char> target, zircon_crash_reason_t reason) {
  StringFile outfile{target};
  const RenderRegion regions = MapReasonToRegions(reason);

  if (static_cast<bool>(regions & RenderRegion::Banner)) {
    uintptr_t crashlog_base_address = 0;
    const char* reason_str;
    switch (reason) {
      case ZirconCrashReason::NoCrash:
        reason_str = "NO CRASH";
        break;

      case ZirconCrashReason::Oom:
        reason_str = "OOM";
        break;

      case ZirconCrashReason::Panic:
        reason_str = "KERNEL PANIC";
        crashlog_base_address = g_crashlog.base_address;
        break;

      case ZirconCrashReason::SoftwareWatchdog:
        reason_str = "SW WATCHDOG";
        break;

      case ZirconCrashReason::UserspaceRootJobTermination:
        reason_str = "USERSPACE ROOT JOB TERMINATION";
        break;

      default:
        reason_str = "UNKNOWN";
        break;
    }
    fprintf(&outfile, "ZIRCON REBOOT REASON (%s)\n\n", reason_str);
    fprintf(&outfile, "UPTIME (ms)\n%" PRIi64 "\n\n", current_time() / ZX_MSEC(1));

    // Keep the format and values in sync with the symbolizer.
    // Print before the registers (KASLR offset).
#if defined(__x86_64__)
    const char* arch = "x86_64";
#elif defined(__aarch64__)
    const char* arch = "aarch64";
#endif
    fprintf(&outfile,
            "VERSION\narch: %s\nbuild_id: %s\ndso: id=%s base=%#lx "
            "name=zircon.elf\n\n",
            arch, version_string(), elf_build_id_string(), crashlog_base_address);
  }

  if (static_cast<bool>(regions & RenderRegion::DebugInfo)) {
    PrintSymbolizerContext(&outfile);

    if (g_crashlog.iframe) {
#if defined(__aarch64__)
      fprintf(&outfile,
              // clang-format off
              "REGISTERS\n"
              "  x0: %#18" PRIx64 "\n"
              "  x1: %#18" PRIx64 "\n"
              "  x2: %#18" PRIx64 "\n"
              "  x3: %#18" PRIx64 "\n"
              "  x4: %#18" PRIx64 "\n"
              "  x5: %#18" PRIx64 "\n"
              "  x6: %#18" PRIx64 "\n"
              "  x7: %#18" PRIx64 "\n"
              "  x8: %#18" PRIx64 "\n"
              "  x9: %#18" PRIx64 "\n"
              " x10: %#18" PRIx64 "\n"
              " x11: %#18" PRIx64 "\n"
              " x12: %#18" PRIx64 "\n"
              " x13: %#18" PRIx64 "\n"
              " x14: %#18" PRIx64 "\n"
              " x15: %#18" PRIx64 "\n"
              " x16: %#18" PRIx64 "\n"
              " x17: %#18" PRIx64 "\n"
              " x18: %#18" PRIx64 "\n"
              " x19: %#18" PRIx64 "\n"
              " x20: %#18" PRIx64 "\n"
              " x21: %#18" PRIx64 "\n"
              " x22: %#18" PRIx64 "\n"
              " x23: %#18" PRIx64 "\n"
              " x24: %#18" PRIx64 "\n"
              " x25: %#18" PRIx64 "\n"
              " x26: %#18" PRIx64 "\n"
              " x27: %#18" PRIx64 "\n"
              " x28: %#18" PRIx64 "\n"
              " x29: %#18" PRIx64 "\n"
              "  lr: %#18" PRIx64 "\n"
              " usp: %#18" PRIx64 "\n"
              " elr: %#18" PRIx64 "\n"
              "spsr: %#18" PRIx64 "\n"
              " esr: %#18" PRIx32 "\n"
              " far: %#18" PRIx64 "\n"
              "\n",
              // clang-format on
              g_crashlog.iframe->r[0], g_crashlog.iframe->r[1], g_crashlog.iframe->r[2],
              g_crashlog.iframe->r[3], g_crashlog.iframe->r[4], g_crashlog.iframe->r[5],
              g_crashlog.iframe->r[6], g_crashlog.iframe->r[7], g_crashlog.iframe->r[8],
              g_crashlog.iframe->r[9], g_crashlog.iframe->r[10], g_crashlog.iframe->r[11],
              g_crashlog.iframe->r[12], g_crashlog.iframe->r[13], g_crashlog.iframe->r[14],
              g_crashlog.iframe->r[15], g_crashlog.iframe->r[16], g_crashlog.iframe->r[17],
              g_crashlog.iframe->r[18], g_crashlog.iframe->r[19], g_crashlog.iframe->r[20],
              g_crashlog.iframe->r[21], g_crashlog.iframe->r[22], g_crashlog.iframe->r[23],
              g_crashlog.iframe->r[24], g_crashlog.iframe->r[25], g_crashlog.iframe->r[26],
              g_crashlog.iframe->r[27], g_crashlog.iframe->r[28], g_crashlog.iframe->r[29],
              g_crashlog.iframe->lr, g_crashlog.iframe->usp, g_crashlog.iframe->elr,
              g_crashlog.iframe->spsr, g_crashlog.esr, g_crashlog.far);
#elif defined(__x86_64__)
      fprintf(&outfile,
              // clang-format off
              "REGISTERS\n"
              "  CS: %#18" PRIx64 "\n"
              " RIP: %#18" PRIx64 "\n"
              " EFL: %#18" PRIx64 "\n"
              " CR2: %#18lx\n"
              " RAX: %#18" PRIx64 "\n"
              " RBX: %#18" PRIx64 "\n"
              " RCX: %#18" PRIx64 "\n"
              " RDX: %#18" PRIx64 "\n"
              " RSI: %#18" PRIx64 "\n"
              " RDI: %#18" PRIx64 "\n"
              " RBP: %#18" PRIx64 "\n"
              " RSP: %#18" PRIx64 "\n"
              "  R8: %#18" PRIx64 "\n"
              "  R9: %#18" PRIx64 "\n"
              " R10: %#18" PRIx64 "\n"
              " R11: %#18" PRIx64 "\n"
              " R12: %#18" PRIx64 "\n"
              " R13: %#18" PRIx64 "\n"
              " R14: %#18" PRIx64 "\n"
              " R15: %#18" PRIx64 "\n"
              "errc: %#18" PRIx64 "\n"
              "\n",
              // clang-format on
              g_crashlog.iframe->cs, g_crashlog.iframe->ip, g_crashlog.iframe->flags, x86_get_cr2(),
              g_crashlog.iframe->rax, g_crashlog.iframe->rbx, g_crashlog.iframe->rcx,
              g_crashlog.iframe->rdx, g_crashlog.iframe->rsi, g_crashlog.iframe->rdi,
              g_crashlog.iframe->rbp, g_crashlog.iframe->user_sp, g_crashlog.iframe->r8,
              g_crashlog.iframe->r9, g_crashlog.iframe->r10, g_crashlog.iframe->r11,
              g_crashlog.iframe->r12, g_crashlog.iframe->r13, g_crashlog.iframe->r14,
              g_crashlog.iframe->r15, g_crashlog.iframe->err_code);
#endif
    } else {
      fprintf(&outfile, "REGISTERS: missing\n");
    }

    fprintf(&outfile, "BACKTRACE (up to 16 calls)\n");

    ktl::span<char> backtrace_target = outfile.available_region();
    size_t len = Thread::Current::AppendBacktrace(backtrace_target.data(), backtrace_target.size());
    outfile.Skip(len);

    fprintf(&outfile, "\n");
  }

  if (static_cast<bool>(regions & RenderRegion::CriticalCounters)) {
    // Include counters for critical events.
    fprintf(&outfile,
            "counters: haf=%" PRId64 " paf=%" PRId64 " pvf=%" PRId64 " lcs=%" PRId64 " lhb=%" PRId64
            " cf=%" PRId64 " \n",
            HandleTableArena::get_alloc_failed_count(), pmm_get_alloc_failed_count(),
            PmmChecker::get_validation_failed_count(), lockup_get_critical_section_oops_count(),
            ChannelDispatcher::get_channel_full_count(), lockup_get_no_heartbeat_oops_count());
  }

  if (static_cast<bool>(regions & RenderRegion::PanicBuffer)) {
    // Include as much of the contents of the panic buffer as we can, if it is
    // not empty.
    //
    // The panic buffer is one of the last thing we print.  Space is limited so
    // if the panic/assert message was long we may not be able to include the
    // whole thing.  That's OK.  The panic buffer is a "nice to have" and we've
    // already printed the primary diagnostics (register dump and backtrace).
    if (panic_buffer.size()) {
      fprintf(&outfile, "panic buffer: %s\n", panic_buffer.c_str());
    } else {
      fprintf(&outfile, "panic buffer: empty\n");
    }
  }

  if (static_cast<bool>(regions & RenderRegion::Dlog)) {
    constexpr ktl::string_view kHeader{"\n--- BEGIN DLOG DUMP ---\n"};
    constexpr ktl::string_view kFooter{"\n--- END DLOG DUMP ---\n"};

    // Finally, if we have been configured to do so, render as much of the
    // recent debug log as we can fit into the crashlog memory.
    outfile.Write(kHeader);

    const ktl::span<char> available_region = outfile.available_region();
    const ktl::span<char> payload_region =
        available_region.size() > kFooter.size()
            ? available_region.subspan(0, available_region.size() - kFooter.size())
            : ktl::span<char>{};

    if (gBootOptions->render_dlog_to_crashlog) {
      outfile.Skip(dlog_render_to_crashlog(payload_region));
    } else {
      StringFile{payload_region}.Write("DLOG -> Crashlog disabled");
    }

    outfile.Write(kFooter);
  }

  return outfile.used_region().size();
}

void crashlog_stash(fbl::RefPtr<VmObject> crashlog) {
  Guard<Mutex> guard{RecoveredCrashlogLock::Get()};
  recovered_crashlog = ktl::move(crashlog);
}

fbl::RefPtr<VmObject> crashlog_get_stashed() {
  Guard<Mutex> guard{RecoveredCrashlogLock::Get()};
  return recovered_crashlog;
}

static void print_recovered_crashlog() {
  fbl::RefPtr<VmObject> crashlog = crashlog_get_stashed();
  if (!crashlog) {
    printf("no recovered crashlog\n");
    return;
  }

  // Allocate a temporary buffer so we can convert the VMO's contents to a C string.
  const size_t buffer_size = crashlog->size() + 1;
  fbl::AllocChecker ac;
  auto buffer = ktl::unique_ptr<char[]>(new (&ac) char[buffer_size]);
  if (!ac.check()) {
    printf("error: failed to allocate %lu for crashlog\n", buffer_size);
    return;
  }

  memset(buffer.get(), 0, buffer_size);
  zx_status_t status = crashlog->Read(buffer.get(), 0, buffer_size - 1);
  if (status != ZX_OK) {
    printf("error: failed to read from recovered crashlog vmo: %d\n", status);
    return;
  }

  printf("recovered crashlog follows...\n");
  printf("%s\n", buffer.get());
  printf("... end of recovered crashlog\n");
}

static int cmd_crashlog(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s dump                              : dump the recovered crashlog\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    print_recovered_crashlog();
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("crashlog", "crashlog", &cmd_crashlog, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(pmm)
