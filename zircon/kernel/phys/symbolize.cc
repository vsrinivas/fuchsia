// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/symbolize.h"

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <stdarg.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/string_view.h>
#include <phys/frame-pointer.h>
#include <phys/stack.h>
#include <pretty/hexdump.h>

// The zx_*_t types used in the exception stuff aren't defined for 32-bit.
// There is no exception handling implementation for 32-bit.
#ifndef __i386__
#include <phys/exception.h>
#endif

namespace {

// On x86-32, there is a fixed link-time address.
#ifdef __i386__
static constexpr auto kLinkTimeAddress = PHYS_LOAD_ADDRESS;
#else
static constexpr uintptr_t kLinkTimeAddress = 0;
#endif

// Arbitrary, but larger than any known format in use.
static constexpr size_t kMaxBuildIdSize = 32;

struct BuildIdNote final {
  static constexpr char kName_[] = "GNU";
  static constexpr uint32_t kType_ = 3;  // NT_GNU_BUILD_ID

  uint32_t n_namesz, n_descsz, n_type;
  alignas(4) char name[sizeof(kName_)];
  alignas(4) uint8_t build_id[/* n_descsz */];

  bool matches() const {
    return (n_type == kType_ &&
            // n_namesz includes the NUL terminator.
            ktl::string_view{name, n_namesz} == ktl::string_view{kName_, sizeof(kName_)});
  }

  const BuildIdNote* next() const {
    return reinterpret_cast<const BuildIdNote*>(&name[(n_namesz + 3) & -4] + ((n_descsz + 3) & -4));
  }
};

// These are defined by the linker script.
extern "C" void __code_start();
extern "C" const BuildIdNote __start_note_gnu_build_id[];
extern "C" const BuildIdNote __stop_note_gnu_build_id[];

class BuildId {
 public:
  auto begin() const { return note_->build_id; }
  auto end() const { return begin() + size(); }
  size_t size() const { return note_->n_descsz; }

  ktl::string_view Print() const { return {hex_, size() * 2}; }

  void Init(const BuildIdNote* start = __start_note_gnu_build_id,
            const BuildIdNote* stop = __stop_note_gnu_build_id) {
    note_ = start;
    ZX_ASSERT(note_->matches());
    ZX_ASSERT(note_->next() <= stop);
    ZX_ASSERT(size() <= kMaxBuildIdSize);
    char* p = hex_;
    for (uint8_t byte : *this) {
      *p++ = "0123456789abcdef"[byte >> 4];
      *p++ = "0123456789abcdef"[byte & 0xf];
    }
  }

  static const BuildId& GetInstance() {
    if (!gInstance.note_) {
      gInstance.Init();
    }
    return gInstance;
  }

 private:
  static BuildId gInstance;

  const BuildIdNote* note_;
  char hex_[kMaxBuildIdSize * 2];
};

BuildId BuildId::gInstance;

#if defined(__aarch64__)

#elif defined(__x86_64__)

#endif

}  // namespace

Symbolize Symbolize::instance_;

void Symbolize::Printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(output_, fmt, args);
  va_end(args);
}

ktl::string_view Symbolize::BuildIdString() { return BuildId::GetInstance().Print(); }

ktl::span<const ktl::byte> Symbolize::BuildId() const {
  const auto& id = BuildId::GetInstance();
  return {reinterpret_cast<const ktl::byte*>(id.begin()),
          reinterpret_cast<const ktl::byte*>(id.end())};
}

void Symbolize::PrintModule() {
  Printf("%s: {{{module:0:%s:elf:%V}}}\n", kProgramName_, kProgramName_,
         BuildId::GetInstance().Print());
}

void Symbolize::PrintMmap() {
  auto start = reinterpret_cast<uintptr_t>(__code_start);
  auto end = reinterpret_cast<uintptr_t>(_end);
  Printf("%s: {{{mmap:%p:%#zx:load:0:rwx:%p}}}\n", kProgramName_, __code_start, end - start,
         kLinkTimeAddress);
}

void Symbolize::ContextAlways() {
  Printf("%s: {{{reset}}}\n", kProgramName_);
  PrintModule();
  PrintMmap();
}

void Symbolize::Context() {
  if (!context_done_) {
    context_done_ = true;
    ContextAlways();
  }
}

void Symbolize::BackTraceFrame(unsigned int n, uintptr_t pc, bool interrupt) {
  // Just print the line in markup format.  Context() was called earlier.
  const char* kind = interrupt ? "pc" : "ra";
  Printf("%s: {{{bt:%u:%#zx:%s}}}\n", kProgramName_, n, pc, kind);
}

void Symbolize::DumpFile(ktl::string_view type, ktl::string_view name, ktl::string_view desc,
                         size_t size_bytes) {
  Context();
  Printf("%s: %V: {{{dumpfile:%V:%V}}} %zu bytes\n", kProgramName_, desc, type, name, size_bytes);
}

void Symbolize::PrintBacktraces(const FramePointer& frame_pointers,
                                const ShadowCallStackBacktrace& shadow_call_stack, unsigned int n) {
  Context();
  if (frame_pointers.empty()) {
    Printf("%s: Frame pointer backtrace is empty!\n", kProgramName_);
  } else {
    Printf("%s: Backtrace (via frame pointers):\n", kProgramName_);
    BackTrace(frame_pointers, n);
  }
  if (BootShadowCallStack::kEnabled) {
    if (shadow_call_stack.empty()) {
      Printf("%s: Shadow call stack backtrace is empty!\n", kProgramName_);
    } else {
      Printf("%s: Backtrace (via shadow call stack):\n", kProgramName_);
    }
    BackTrace(shadow_call_stack, n);
  }
}

void Symbolize::PrintStack(uintptr_t sp, ktl::optional<size_t> max_size_bytes) {
  const size_t configured_max = gBootOptions->phys_print_stack_max;
  auto dump_stack = [max = max_size_bytes.value_or(configured_max), sp, this](
                        const BootStack& stack, const char* which) {
    Printf("%s: Partial dump of %s stack at [%p, %p):\n", kProgramName_, which, &stack, &stack + 1);
    ktl::span whole(reinterpret_cast<const uint64_t*>(stack.stack),
                    sizeof(stack.stack) / sizeof(uint64_t));
    const uintptr_t base = reinterpret_cast<uintptr_t>(whole.data());
    ktl::span used = whole.subspan((sp - base) / sizeof(uint64_t));
    hexdump(used.data(), ktl::min(max, used.size_bytes()));
  };

  if (boot_stack.IsOnStack(sp)) {
    dump_stack(boot_stack, "boot");
  } else if (phys_exception_stack.IsOnStack(sp)) {
    dump_stack(phys_exception_stack, "exception");
  } else {
    Printf("%s: Stack pointer is outside expected bounds [%p, %p) or [%p, %p)\n", kProgramName_,
           &boot_stack, &boot_stack + 1, &phys_exception_stack, &phys_exception_stack + 1);
  }
}

#ifndef __i386__

void Symbolize::PrintRegisters(const PhysExceptionState& exc) {
  Printf("%s: Registers stored at %p: {{{hexdump:", kProgramName_, &exc);

#if defined(__aarch64__)

  for (size_t i = 0; i < ktl::size(exc.regs.r); ++i) {
    if (i % 4 == 0) {
      Printf("\n%s: ", kProgramName_);
    }
    Printf("  %sX%zu: 0x%016" PRIx64, i < 10 ? " " : "", i, exc.regs.r[i]);
  }
  Printf("  X30: 0x%016" PRIx64 "\n", exc.regs.lr);
  Printf("%s:    SP: 0x%016" PRIx64 "   PC: 0x%016" PRIx64 " SPSR: 0x%016" PRIx64 "\n",
         kProgramName_, exc.regs.sp, exc.regs.pc, exc.regs.cpsr);
  Printf("%s:   ESR: 0x%016" PRIx64 "  FAR: 0x%016" PRIx64 "\n", kProgramName_,
         exc.exc.arch.u.arm_64.esr, exc.exc.arch.u.arm_64.far);

#elif defined(__x86_64__)

  Printf("%s:  RAX: 0x%016" PRIx64 " RBX: 0x%016" PRIx64 " RCX: 0x%016" PRIx64 " RDX: 0x%016" PRIx64
         "\n",
         kProgramName_, exc.regs.rax, exc.regs.rbx, exc.regs.rcx, exc.regs.rdx);
  Printf("%s:  RSI: 0x%016" PRIx64 " RDI: 0x%016" PRIx64 " RBP: 0x%016" PRIx64 " RSP: 0x%016" PRIx64
         "\n",
         kProgramName_, exc.regs.rsi, exc.regs.rdi, exc.regs.rbp, exc.regs.rsp);
  Printf("%s:   R8: 0x%016" PRIx64 "  R9: 0x%016" PRIx64 " R10: 0x%016" PRIx64 " R11: 0x%016" PRIx64
         "\n",
         kProgramName_, exc.regs.r8, exc.regs.r9, exc.regs.r10, exc.regs.r11);
  Printf("%s:  R12: 0x%016" PRIx64 " R13: 0x%016" PRIx64 " R14: 0x%016" PRIx64 " R15: 0x%016" PRIx64
         "\n",
         kProgramName_, exc.regs.r12, exc.regs.r13, exc.regs.r14, exc.regs.r15);
  Printf("%s:  RIP: 0x%016" PRIx64 " RFLAGS: 0x%08" PRIx64 " FS.BASE: 0x%016" PRIx64
         " GS.BASE: 0x%016" PRIx64 "\n",
         kProgramName_, exc.regs.rip, exc.regs.rflags, exc.regs.fs_base, exc.regs.gs_base);
  Printf("%s:   V#: " PRIu64 "  ERR: %#" PRIx64 "  CR2: %016" PRIx64 "\n", kProgramName_,
         exc.exc.arch.u.x86_64.vector, exc.exc.arch.u.x86_64.err_code, exc.exc.arch.u.x86_64.cr2);

#endif

  Printf("%s: }}}\n", kProgramName_);
}

void Symbolize::PrintException(uint64_t vector, const char* vector_name,
                               const PhysExceptionState& exc) {
  Printf("%s: exception vector %s (%#" PRIx64 ")\n", Symbolize::kProgramName_, vector_name, vector);

  // Always print the context, even if it was printed earlier.
  context_done_ = false;
  Context();

  PrintRegisters(exc);

  BackTraceFrame(0, exc.pc(), true);

  // Collect each kind of backtrace if possible.
  FramePointer fp_backtrace;
  ShadowCallStackBacktrace scs_backtrace;

  const uint64_t fp = exc.fp();
  auto fp_on = [fp](const BootStack& stack) {
    return stack.IsOnStack(fp) && stack.IsOnStack(fp + sizeof(FramePointer));
  };
  if (fp % sizeof(uintptr_t) == 0 && (fp_on(boot_stack) || fp_on(phys_exception_stack))) {
    fp_backtrace = *reinterpret_cast<FramePointer*>(fp);
  }

  uint64_t scsp = exc.shadow_call_sp();
  scs_backtrace = boot_shadow_call_stack.BackTrace(scsp);
  if (scs_backtrace.empty()) {
    scs_backtrace = phys_exception_shadow_call_stack.BackTrace(scsp);
  }

  // Print whatever we have.
  PrintBacktraces(fp_backtrace, scs_backtrace);

  PrintStack(exc.sp());
}

void PrintPhysException(uint64_t vector, const char* vector_name, const PhysExceptionState& regs) {
  Symbolize::GetInstance()->PrintException(vector, vector_name, regs);
}

#endif  // !__i386__
