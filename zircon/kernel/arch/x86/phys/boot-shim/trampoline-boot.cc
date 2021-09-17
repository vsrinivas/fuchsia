// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "trampoline-boot.h"

#include <lib/arch/x86/standard-segments.h>
#include <lib/arch/zbi-boot.h>
#include <lib/memalloc/pool.h>
#include <lib/zbitl/items/mem_config.h>

#include <cstddef>
#include <cstring>

#include <ktl/byte.h>
#include <phys/page-table.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>

// This describes the "trampoline" area that is set up in some memory that's
// safely out of the way: not part of this shim's own image, which might be
// overwritten, and not part of the fixed-position kernel load image or reserve
// memory, not part of the kernel image being relocated, and not part of the
// data ZBI image.  Trampoline::size() bytes must be allocated in the safe
// place and then it must be constructed with new (ptr) (Trampoline::size())
// before Boot() is finally called.
class TrampolineBoot::Trampoline {
 public:
  explicit Trampoline(size_t space) {
    ZX_ASSERT(space >= size());
    const zbitl::ByteView code = TrampolineCode();
    memcpy(code_, code.data(), code.size());
  }

  static size_t size() { return offsetof(Trampoline, code_) + TrampolineCode().size(); }

  [[noreturn]] void Boot(const zircon_kernel_t* kernel, uint32_t kernel_size, void* zbi) {
    TrampolineArgs args = {
        .dst = kFixedLoadAddress,
        .src = reinterpret_cast<uintptr_t>(kernel),
        .count = kernel_size,
        .entry = static_cast<uintptr_t>(kernel->data_kernel.entry),
        .zbi = reinterpret_cast<uintptr_t>(zbi),
    };
    args.SetDirection();
    ZX_ASSERT(args.entry == kernel->data_kernel.entry);
    arch::ZbiBootRaw(reinterpret_cast<uintptr_t>(code_), &args);
  }

 private:
  // This packs up the arguments for the trampoline code, which are pretty much
  // the operands for REP MOVSB plus the entry point and data ZBI addresses.
  struct TrampolineArgs {
    // When the addresses overlap, the copying can be done backwards and so the
    // direction flag is set for REP MOVSB and the starting pointers are at the
    // laste byte rather than the first.
    void SetDirection() {
      backwards = dst > src && dst - src < count;
      if (backwards) {
        dst += count - 1;
        src += count - 1;
      }
    }

    uint64_t dst;
    uint64_t src;
    uint64_t count;
    uint64_t entry;
    uint64_t zbi;
    bool backwards;
  };

  [[gnu::const]] static zbitl::ByteView TrampolineCode() {
    // This tiny bit of code will be copied someplace out of the way.  Then it
    // will be entered with %rsi pointing at TrampolineArgs, which can be on
    // the stack since it's read immediately.  Since this code is safely out of
    // the way, it can perform a copy that might clobber this boot shim's own
    // code, data, bss, and stack.  After the copy, it jumps directly to the
    // fixed-address ZBI kernel's entry point and %rsi points to the data ZBI.
    //
    // First the code loads the backwards flag into %al, the entry address into
    // %rbx, and the ZBI address into %rdx.  Then it loads the registers used
    // by REP MOVSB (%rcx, %rdi, and %rsi).  It then tests the %al flag to set
    // the Direction flag (STD) for backwards mode.  Then REP MOVSB does the
    // copy, whether forwards or backwards.  After that, the SP and FP are
    // cleared, the D flag is cleared again and interrupts disabled for good
    // measure, before finally moving the ZBI pointer into place (%rsi) and
    // jumping to the entry point (%rbx).
    const std::byte* code;
    size_t size;
    __asm__(R"""(
.code64
.pushsection .rodata.trampoline, "a?", %%progbits
0:
  mov %c[backwards](%%rsi), %%al
  mov %c[entry](%%rsi), %%rbx
  mov %c[count](%%rsi), %%rcx
  mov %c[zbi](%%rsi), %%rdx
  mov %c[dst](%%rsi), %%rdi
  mov %c[src](%%rsi), %%rsi
  testb %%al, %%al
  jz 1f
  std
1:
  rep movsb
  xor %%esp, %%esp
  xor %%ebp, %%ebp
  cld
  cli
  mov %%rdx, %%rsi
  jmp *%%rbx
2:
.popsection
)"""
#ifdef __i386__
            R"""(
.code32
  mov $0b, %[code]
  mov $(2b - 0b), %[size]
            )"""
#else
            R"""(
  lea 0b(%%rip), %[code]
  mov $(2b - 0b), %[size]
            )"""
#endif
            : [code] "=r"(code), [size] "=r"(size)
            : [backwards] "i"(offsetof(TrampolineArgs, backwards)),  //
              [dst] "i"(offsetof(TrampolineArgs, dst)),              //
              [src] "i"(offsetof(TrampolineArgs, src)),              //
              [count] "i"(offsetof(TrampolineArgs, count)),          //
              [zbi] "i"(offsetof(TrampolineArgs, zbi)),              //
              [entry] "i"(offsetof(TrampolineArgs, entry)));
    return {code, size};
  }

  arch::X86StandardSegments segments_;
  ktl::byte code_[];
};

fitx::result<BootZbi::Error> TrampolineBoot::Load(uint32_t extra_data_capacity) {
  if (KernelHeader()->entry < kFixedLoadAddress) {
    // New-style position-independent kernel.
    return BootZbi::Load(extra_data_capacity);
  }

  // Now we know how much space the kernel image needs.
  // Reserve it at the fixed load address.
  auto& pool = Allocation::GetPool();
  if (auto result = pool.UpdateFreeRamSubranges(memalloc::Type::kFixedAddressKernel,
                                                kFixedLoadAddress, KernelMemorySize());
      result.is_error()) {
    return fitx::error{BootZbi::Error{.zbi_error = "unable to reserve kernel's load image"sv}};
  }

  // The trampoline needs someplace safely neither in the kernel image, nor in
  // the data ZBI image, nor in this shim's own image since that might overlap
  // the fixed-address target region.  It's tiny, so just extend the extra data
  // capacity to cover it and use the few bytes just after the data ZBI.  The
  // space is safely allocated in our present reckoning so it's disjoint from
  // the data and kernel image memory and from this shim's own image, but as
  // soon as we boot into the new kernel it will be reclaimable memory.
  if (auto result = BootZbi::Load(extra_data_capacity + Trampoline::size(), kFixedLoadAddress);
      result.is_error()) {
    return result.take_error();
  }

  auto extra_space = DataZbi().storage().subspan(DataZbi().size_bytes());
  auto trampoline = extra_space.subspan(extra_data_capacity);
  trampoline_ = new (trampoline.data()) Trampoline(trampoline.size());

  // In the x86-64 case, we set up page-tables out of the .bss, which must
  // persist after booting the next kernel payload; however, this part of the
  // .bss might be clobbered by that self-same fixed load image. To avoid that
  // issue, now that physical memory management as been bootstrapped, we re-set
  // up the address space out of the allocator, which will avoid allocating
  // from out of the load image's range that we just reserved.
#ifdef __x86_64__
  ArchSetUpAddressSpaceLate();
#endif

  return fitx::ok();
}

[[noreturn]] void TrampolineBoot::Boot(ktl::optional<void*> argument) {
  ZX_ASSERT(!MustRelocateDataZbi());

  uintptr_t entry = static_cast<uintptr_t>(KernelEntryAddress());
  ZX_ASSERT(entry == KernelEntryAddress());

  uintptr_t zbi = static_cast<uintptr_t>(DataLoadAddress());
  ZX_ASSERT(zbi == DataLoadAddress());

  uintptr_t kernel_first = static_cast<uintptr_t>(KernelLoadAddress());
  uintptr_t kernel_last = static_cast<uintptr_t>(KernelLoadAddress() + KernelLoadSize() - 1);
  ZX_ASSERT(kernel_first == KernelLoadAddress());
  ZX_ASSERT(kernel_last == KernelLoadAddress() + KernelLoadSize() - 1);

  uintptr_t kernel_size = static_cast<uintptr_t>(KernelLoadSize());
  ZX_ASSERT(kernel_size == KernelLoadSize());

  uintptr_t fixed_first = static_cast<uintptr_t>(kFixedLoadAddress);
  uintptr_t fixed_last = static_cast<uintptr_t>(kFixedLoadAddress + KernelLoadSize() - 1);
  ZX_ASSERT(fixed_first == kFixedLoadAddress);
  ZX_ASSERT(fixed_last == kFixedLoadAddress + KernelLoadSize() - 1);

  if (!trampoline_) {
    // This is a new-style position-independent kernel.  Boot it where it is.
    BootZbi::Boot(argument);
  }

  LogAddresses();
  LogFixedAddresses();
  LogBoot(KernelEntryAddress());

  trampoline_->Boot(KernelImage(), KernelLoadSize(), argument.value_or(DataZbi().storage().data()));
}

// This output lines up with what BootZbi::LogAddresses() prints.
void TrampolineBoot::LogFixedAddresses() const {
#define ADDR "0x%016" PRIx64
  const auto& name = Symbolize::kProgramName_;
  const uint64_t kernel = kFixedLoadAddress;
  const uint64_t bss = kernel + KernelLoadSize();
  const uint64_t end = kernel + KernelMemorySize();
  debugf("%s: Relocated @ [" ADDR ", " ADDR ")\n", name, kernel, bss);
  debugf("%s:       BSS @ [" ADDR ", " ADDR ")\n", name, bss, end);
}
