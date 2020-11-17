// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/crashlog.h"

#include <ctype.h>
#include <inttypes.h>
#include <lib/console.h>
#include <lib/lockup_detector.h>
#include <lib/version.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <zircon/boot/crash-reason.h>

#include <arch/regs.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <object/handle.h>
#include <vm/pmm.h>
#include <vm/pmm_checker.h>
#include <vm/vm.h>

crashlog_t crashlog = {};

PanicBuffer panic_buffer;

FILE stdout_panic_buffer{[](void*, ktl::string_view str) {
                           panic_buffer.Append(str);
                           return stdout->Write(str);
                         },
                         nullptr};

namespace {

DECLARE_SINGLETON_MUTEX(RecoveredCrashlogLock);
fbl::RefPtr<VmObject> recovered_crashlog TA_GUARDED(RecoveredCrashlogLock::Get());

}  // namespace

size_t crashlog_to_string(char* out, const size_t out_len, zircon_crash_reason_t reason) {
  struct OutFile {
    // This holds the remaining buffer available to write.
    ktl::span<char> buffer_;

    // This adapts the FILE* interface so it calls Write, below.
    FILE stream_{this};

    int Write(ktl::string_view str) {
      // Copy as much as there is space for.
      str = str.substr(0, ktl::min(str.size(), buffer_.size()));
      if (str.empty()) {
        // If there's no space at all, return error so fprintf bails out early.
        return -1;
      }
      memcpy(buffer_.data(), str.data(), str.size());

      // Leave only the remaining buffer space.
      buffer_ = buffer_.subspan(str.size());

      return static_cast<int>(str.size());
    }
  } outfile{{out, out_len}};
  auto total_size = [&]() -> size_t { return outfile.buffer_.data() - out; };

  const bool is_oom = (reason == ZirconCrashReason::Oom);

  fprintf(&outfile.stream_, "ZIRCON REBOOT REASON (%s)\n\n", is_oom ? "OOM" : "KERNEL PANIC");

  fprintf(&outfile.stream_, "UPTIME (ms)\n%" PRIi64 "\n\n", current_time() / ZX_MSEC(1));

  // Keep the format and values in sync with the symbolizer.
  // Print before the registers (KASLR offset).
#if defined(__x86_64__)
  const char* arch = "x86_64";
#elif defined(__aarch64__)
  const char* arch = "aarch64";
#endif
  fprintf(&outfile.stream_,
          "VERSION\narch: %s\nbuild_id: %s\ndso: id=%s base=%#lx "
          "name=zircon.elf\n\n",
          arch, version_string(), elf_build_id_string(), is_oom ? 0u : crashlog.base_address);

  if (is_oom) {
    // If OOM, then including a backtrace doesn't make sense, return early.
    return total_size();
  }

  PrintSymbolizerContext(&outfile.stream_);

  if (crashlog.iframe) {
#if defined(__aarch64__)
    fprintf(&outfile.stream_,
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
            crashlog.iframe->r[0], crashlog.iframe->r[1], crashlog.iframe->r[2],
            crashlog.iframe->r[3], crashlog.iframe->r[4], crashlog.iframe->r[5],
            crashlog.iframe->r[6], crashlog.iframe->r[7], crashlog.iframe->r[8],
            crashlog.iframe->r[9], crashlog.iframe->r[10], crashlog.iframe->r[11],
            crashlog.iframe->r[12], crashlog.iframe->r[13], crashlog.iframe->r[14],
            crashlog.iframe->r[15], crashlog.iframe->r[16], crashlog.iframe->r[17],
            crashlog.iframe->r[18], crashlog.iframe->r[19], crashlog.iframe->r[20],
            crashlog.iframe->r[21], crashlog.iframe->r[22], crashlog.iframe->r[23],
            crashlog.iframe->r[24], crashlog.iframe->r[25], crashlog.iframe->r[26],
            crashlog.iframe->r[27], crashlog.iframe->r[28], crashlog.iframe->r[29],
            crashlog.iframe->lr, crashlog.iframe->usp, crashlog.iframe->elr, crashlog.iframe->spsr,
            crashlog.esr, crashlog.far);
#elif defined(__x86_64__)
    fprintf(&outfile.stream_,
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
            crashlog.iframe->cs, crashlog.iframe->ip, crashlog.iframe->flags,
            x86_get_cr2(), crashlog.iframe->rax, crashlog.iframe->rbx,
            crashlog.iframe->rcx, crashlog.iframe->rdx, crashlog.iframe->rsi,
            crashlog.iframe->rdi, crashlog.iframe->rbp,
            crashlog.iframe->user_sp, crashlog.iframe->r8,
            crashlog.iframe->r9, crashlog.iframe->r10, crashlog.iframe->r11,
            crashlog.iframe->r12, crashlog.iframe->r13, crashlog.iframe->r14,
            crashlog.iframe->r15, crashlog.iframe->err_code);
    // clang-format on
#endif
  }

  fprintf(&outfile.stream_, "BACKTRACE (up to 16 calls)\n");

  size_t len = Thread::Current::AppendBacktrace(outfile.buffer_.data(), outfile.buffer_.size());
  outfile.buffer_ = outfile.buffer_.subspan(len);
  fprintf(&outfile.stream_, "\n");

  // Include counters for critical events.
  fprintf(&outfile.stream_,
          "counters: haf=%" PRId64 " paf=%" PRId64 " pvf=%" PRId64 " lcs=%" PRId64 " lhb=%" PRId64
          " \n",
          HandleTableArena::get_alloc_failed_count(), pmm_get_alloc_failed_count(),
          PmmChecker::get_validation_failed_count(), lockup_get_critical_section_oops_count(),
          lockup_get_no_heartbeat_oops_count());

  // Finally, include the contents of the panic buffer (which may be empty).
  //
  // The panic buffer is the last thing we print.  Space is limited so if the panic/assert message
  // was long we may not be able to include the whole thing.  That's OK.  The panic buffer is a
  // "nice to have" and we've already printed the primary diagnostics (register dump and backtrace).
  fprintf(&outfile.stream_, "panic buffer: %s\n", panic_buffer.c_str());

  fprintf(&outfile.stream_, "\n");

  return total_size();
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
