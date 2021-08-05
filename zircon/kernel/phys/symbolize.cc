// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/symbolize.h"

#include <lib/boot-options/boot-options.h>
#include <stdarg.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/string_view.h>
#include <phys/frame-pointer.h>
#include <phys/stack.h>
#include <pretty/hexdump.h>

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

}  // namespace

Symbolize Symbolize::instance_;

void Symbolize::Printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(output_, fmt, args);
  va_end(args);
}

ktl::string_view Symbolize::BuildIdString() { return BuildId::GetInstance().Print(); }

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

void Symbolize::BackTraceFrame(unsigned int n, uintptr_t pc) {
  // Just print the line in markup format.  Context() was called earlier.
  Printf("%s: {{{bt:%u:%#zx}}}\n", kProgramName_, n, pc);
}

void Symbolize::DumpFile(ktl::string_view type, ktl::string_view name) {
  Context();
  Printf("%s: {{{dumpfile:%V:%V}}}\n", kProgramName_, type, name);
}

void Symbolize::PrintBacktraces(const FramePointer& frame_pointers,
                                const ShadowCallStackBacktrace& shadow_call_stack) {
  Context();
  if (frame_pointers.empty()) {
    Printf("%s: Frame pointer backtrace is empty!\n", kProgramName_);
  } else {
    Printf("%s: Backtrace (via frame pointers):\n", kProgramName_);
    BackTrace(frame_pointers);
  }
  if (BootShadowCallStack::kEnabled) {
    if (shadow_call_stack.empty()) {
      Printf("%s: Shadow call stack backtrace is empty!\n", kProgramName_);
    } else {
      Printf("%s: Backtrace (via shadow call stack):\n", kProgramName_);
    }
    BackTrace(shadow_call_stack);
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
  } else {
    Printf("%s: Stack pointer is outside expected bounds [%p, %p)\n", kProgramName_, &boot_stack,
           &boot_stack + 1);
  }
}
