// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "symbolize.h"

#include <stdarg.h>
#include <stdint.h>
#include <zircon/assert.h>

#include <ktl/string_view.h>

#include "stack.h"

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
extern "C" char _end[];
extern "C" const BuildIdNote __start_note_gnu_build_id[];
extern "C" const BuildIdNote __stop_note_gnu_build_id[];

class BuildId {
 public:
  BuildId(const BuildIdNote* start = __start_note_gnu_build_id,
          const BuildIdNote* stop = __stop_note_gnu_build_id)
      : note_(start) {
    ZX_ASSERT(note_->matches());
    ZX_ASSERT(note_->next() <= stop);
  }

  auto begin() const { return note_->build_id; }
  auto end() const { return begin() + size(); }
  size_t size() const { return note_->n_descsz; }

  ktl::string_view Print() {
    ZX_ASSERT(size() <= kMaxBuildIdSize);
    char* p = hex_;
    for (uint8_t byte : *this) {
      *p++ = "0123456789abcdef"[byte >> 4];
      *p++ = "0123456789abcdef"[byte & 0xf];
    }
    return {hex_, size() * 2};
  }

 private:
  const BuildIdNote* note_ = nullptr;
  char hex_[kMaxBuildIdSize * 2];
};

}  // namespace

Symbolize Symbolize::instance_;

void Symbolize::Printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(output_, fmt, args);
  va_end(args);
}

void Symbolize::PrintModule() {
  Printf("{{{module:0:%s:elf:%V}}}\n", kProgramName_, BuildId().Print());
}

void Symbolize::PrintMmap() {
  auto start = reinterpret_cast<uintptr_t>(__code_start);
  auto end = reinterpret_cast<uintptr_t>(_end);
  Printf("{{{mmap:%p:%#zx:load:0:rwx:%p}}}\n", __code_start, end - start, kLinkTimeAddress);
}

void Symbolize::ContextAlways() {
  Printf("{{{reset}}}\n");
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
  Printf("{{{bt:%u:%#zx}}}\n", n, pc);
}

void Symbolize::DumpFile(ktl::string_view type, ktl::string_view name) {
  Context();
  Printf("{{{dumpfile:%V:%V}}}\n", type, name);
}
