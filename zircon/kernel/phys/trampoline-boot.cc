// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/trampoline-boot.h"

#include <lib/arch/zbi-boot.h>
#include <lib/memalloc/pool.h>
#include <lib/zbitl/items/mem-config.h>
#include <zircon/assert.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <fbl/algorithm.h>
#include <ktl/byte.h>
#include <phys/page-table.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>

#include <ktl/enforce.h>

namespace {

#if defined(__x86_64__) || defined(__i386__)
// In the legacy fixed-address format, the entry address is always above 1M.
// In the new format, it's an offset and in practice it's never > 1M.  So
// this is a safe-enough heuristic to distinguish the new from the ol
bool IsLegacyEntryAddress(uint64_t address) { return address > TrampolineBoot::kLegacyLoadAddress; }

// Relocated blob size must be aligned to |kRelocateAlign|.
constexpr size_t kRelocateAlign = 1;

// When a RelocatedTarget is copied forward, source and destination offsets must be adjusted by
// |kForwardBiad|.
constexpr int64_t kForwardBias = 0;

// When a RelocatedTarget is copied backwards, source and destination offsets must be adjusted by
// |kForwardBiad|.
constexpr int64_t kBackwardBias = -1;

#else

// ARM does not use legacy fixed address format.
bool IsLegacyEntryAddress(uint64_t address) { return false; }

// Relocated blob size must be aligned to |kRelocateAlign|.
constexpr size_t kRelocateAlign = 32;

// When a RelocatedTarget is copied forward, source and destination offsets must be adjusted by
// |kForwardBiad|.
constexpr int64_t kForwardBias = -16;

// When a RelocatedTarget is copied backwards, source and destination offsets must be adjusted by
// |kForwardBiad|.
constexpr int64_t kBackwardBias = 0;

#endif

struct RelocateTarget {
  RelocateTarget() = default;

  RelocateTarget(uintptr_t destination, ktl::span<const ktl::byte> blob)
      : src(reinterpret_cast<uintptr_t>(blob.data())),
        dst(destination),
        count(fbl::round_up(blob.size(), kRelocateAlign)),
        backwards(dst > src && dst - src < count) {
    if (backwards) {
      dst += count + kBackwardBias;
      src += count + kBackwardBias;
    } else {
      dst += kForwardBias;
      src += kForwardBias;
    }
  }

  constexpr uint64_t destination() const {
    return backwards ? dst - count - kBackwardBias : dst - kForwardBias;
  }

  uint64_t src = 0;
  uint64_t dst = 0;
  uint64_t count = 0;

  // When the addresses overlap, the copying can be done backwards and so the
  // direction flag is set for REP MOVSB and the starting pointers are at the
  // last byte rather than the first. While this is a boolean flag, we can
  // use fewer ASM instruction in the inline assembly by increasinng its width.
  uint64_t backwards = 0;
};

#if __aarch64__

static_assert(offsetof(RelocateTarget, src) == offsetof(RelocateTarget, dst) - sizeof(uint64_t),
              "Must be contiguous for arm64 ldp instruction.");
static_assert(offsetof(RelocateTarget, count) ==
                  offsetof(RelocateTarget, backwards) - sizeof(uint64_t),
              "Must be contiguous for arm64 ldp instruction.");
#endif

}  // namespace

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

  [[noreturn]] void Boot(RelocateTarget kernel, RelocateTarget zbi, uint64_t entry_address) {
    args = {
        .kernel = kernel,
        .zbi = zbi,
        .data_zbi = zbi.destination(),
        .entry = entry_address,
    };
    ZX_ASSERT(args.entry == entry_address);
    arch::ZbiBootRaw(reinterpret_cast<uintptr_t>(code_), &args);
  }

 private:
  // This packs up the arguments for the trampoline code, which are pretty much
  // the operands for REP MOVSB plus the entry point and data ZBI addresses.
  struct TrampolineArgs {
    RelocateTarget kernel;
    RelocateTarget zbi;
    uint64_t data_zbi;
    uint64_t entry;
  };

  // We must require the compiler not to inline |TrampolineCode| to prevent
  // more that one instance of |TrampolineCode| to exist. The real issue,
  // is that inlining may introduce alignment or jump relaxation between
  // instances causing the size of the assembler code to be different.
  [[gnu::const, gnu::noinline]] static zbitl::ByteView TrampolineCode() {
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
    const ktl::byte* code;
    size_t size;
#if defined(__x86_64__) || defined(__i386__)
    __asm__(
        R"""(
.code64
.pushsection .rodata.trampoline, "a?", %%progbits
0:
  # Save |rsi| in |rbx|, where |rbx| will always point to '&args'.
  mov %%rsi, %%rbx
  mov %c[zbi_count](%%rbx), %%rcx
  test %%rcx, %%rcx
  jz 2f
  mov %c[zbi_dst](%%rbx), %%rdi
  mov %c[zbi_src](%%rbx), %%rsi
  cmp %%rdi, %%rsi
  je 2f
  mov %c[zbi_backwards](%%rbx), %%al
  testb %%al,%%al
  jz 1f
  std
1:
  rep movsb
  cld
2:
  mov %c[kernel_count](%%rbx), %%rcx
  mov %c[kernel_dst](%%rbx), %%rdi
  mov %c[kernel_src](%%rbx), %%rsi
  cmp %%rdi, %%rsi
  je 4f
  mov %c[kernel_backwards](%%rbx), %%al
  testb %%al, %%al
  jz 3f
  std
3:
  rep movsb
4:
  # Clean stack pointers before jumping into the kernel.
  xor %%esp, %%esp
  xor %%ebp, %%ebp
  cld
  cli
  # The data ZBI must be in rsi before jumping into the kernel entry address.
  mov %c[data_zbi](%%rbx), %%rsi
  mov %c[entry](%%rbx), %%rbx
  jmp *%%rbx
4:
.popsection
)"""

#ifdef __i386__
        R"""(
.code32
  mov $0b, %[code]
  mov $(4b - 0b), %[size]
            )"""
#else
        R"""(
  lea 0b(%%rip), %[code]
  mov $(4b - 0b), %[size]
            )"""
#endif

        : [code] "=r"(code), [size] "=r"(size)
        : [kernel_backwards] "i"(offsetof(TrampolineArgs, kernel.backwards)),  //
          [kernel_dst] "i"(offsetof(TrampolineArgs, kernel.dst)),              //
          [kernel_src] "i"(offsetof(TrampolineArgs, kernel.src)),              //
          [kernel_count] "i"(offsetof(TrampolineArgs, kernel.count)),          //
          [zbi_dst] "i"(offsetof(TrampolineArgs, zbi.dst)),                    //
          [zbi_src] "i"(offsetof(TrampolineArgs, zbi.src)),                    //
          [zbi_count] "i"(offsetof(TrampolineArgs, zbi.count)),                //
          [zbi_backwards] "i"(offsetof(TrampolineArgs, zbi.backwards)),        //
          [data_zbi] "i"(offsetof(TrampolineArgs, data_zbi)),                  //
          [entry] "i"(offsetof(TrampolineArgs, entry)));
#elif __aarch64__  // arm64
    __asm__(
        R""(
.pushsection .rodata.trampoline, "a?", %%progbits
// x0 contains |&args|.
.Ltrampoline_start.%=:
  mov x10, x0
  ldp x0, x1, [x10, %[zbi_dst_offset]]
.Ltrampoline_zbi.%=:
  add x9, x10, %[data_offset]
  bl .Lcopy_start.%=
.Ltrampoline_kernel.%=:
  add x9, x10, %[kernel_offset]
  bl .Lcopy_start.%=
.Ltrampoline_exit.%=:
  mov x29, xzr
  mov x30, xzr
  mov sp, x29
  br x1

// Expectation:
//   x9: RelocatableTarget*
//   x2-x8 are used during this procedure.
.Lcopy_start.%=:
  // x2 -> src address
  // x3 -> dst address
  // x4 -> count (in bytes)
  // x5 -> backwards (direction)
  ldp x2, x3, [x9]
  ldp x4, x5, [x9, %[count_offset]]
  cbz x4, .Lcopy_ret.%=
  cmp x2, x3
  beq .Lcopy_ret.%=
  // test direction flag.
  cbnz x5, .Lcopy_backwards.%=

// x2 and x3 hold the first byte in the range to copy, and x4 holds the number of bytes,
// which is a multiple of 32.
.Lcopy_forward.%=:
  ldp x5, x6, [x2, #16]
  ldp x7, x8, [x2, #32]!
  stp x5, x6, [x3, #16]
  stp x7, x8, [x3, #32]!
  sub x4, x4, #32
  cbnz x4, .Lcopy_forward.%=
  ret

// In backwards mode, the src and dst registers point the last, non inclusive, byte and
// is guaranteed to be a multiple of 32b, hence we can just loop.
.Lcopy_backwards.%=:
  ldp x5, x6, [x2, #-16]
  ldp x7, x8, [x2, #-32]!
  stp x5, x6, [x3, #-16]
  stp x7, x8, [x3, #-32]!
  sub x4, x4, #32
  cbnz x4, .Lcopy_backwards.%=
.Lcopy_ret.%=:
  ret

// Used to calculate code size.
.Ltrampoline_end.%=:
.popsection

adrp %[code], .Ltrampoline_start.%=
add %[code], %[code], #:lo12:.Ltrampoline_start.%=
mov %[size], (.Ltrampoline_end.%= - .Ltrampoline_start.%=)
        )""
        : [code] "=r"(code), [size] "=r"(size)
        : [kernel_offset] "i"(offsetof(TrampolineArgs, kernel)),
          [data_offset] "i"(offsetof(TrampolineArgs, zbi)),
          [src_offset] "i"(offsetof(RelocateTarget, src)),
          [count_offset] "i"(offsetof(RelocateTarget, count)),
          [zbi_dst_offset] "i"(offsetof(TrampolineArgs, data_zbi)),
          [entry] "i"(offsetof(TrampolineArgs, entry)));
#endif
    return {code, size};
  }

  TrampolineArgs args;
  ktl::byte code_[];
};

void TrampolineBoot::SetKernelAddresses() {
  kernel_entry_address_ = BootZbi::KernelEntryAddress();
  if (IsLegacyEntryAddress(KernelHeader()->entry)) {
    set_kernel_load_address(kLegacyLoadAddress);
    kernel_entry_address_ = KernelHeader()->entry;
  }
}

fit::result<BootZbi::Error> TrampolineBoot::Load(uint32_t extra_data_capacity,
                                                 ktl::optional<uint64_t> kernel_load_address,
                                                 ktl::optional<uint64_t> data_load_address) {
  if (kernel_load_address) {
    set_kernel_load_address(*kernel_load_address);
  }

  if (data_load_address) {
    data_load_address_ = data_load_address;
  }

  if (!kernel_load_address_) {
    // New-style position-independent kernel.
    return BootZbi::Load(extra_data_capacity);
  }

  // Now we know how much space the kernel image needs.
  // Reserve it at the fixed load address.
  auto& pool = Allocation::GetPool();
  if (auto result = pool.UpdateFreeRamSubranges(memalloc::Type::kFixedAddressKernel,
                                                *kernel_load_address_, KernelMemorySize());
      result.is_error()) {
    return fit::error{BootZbi::Error{.zbi_error = "unable to reserve kernel's load image"sv}};
  }

  if (data_load_address_) {
    if (auto result = pool.UpdateFreeRamSubranges(memalloc::Type::kDataZbi, *data_load_address_,
                                                  DataLoadSize() + extra_data_capacity);
        result.is_error()) {
      return fit::error{BootZbi::Error{.zbi_error = "unable to reserve data ZBI's load image"sv}};
    }
  }

  // The trampoline needs someplace safely neither in the kernel image, nor in
  // the data ZBI image, nor in this shim's own image since that might overlap
  // the fixed-address target region.  It's tiny, so just extend the extra data
  // capacity to cover it and use the few bytes just after the data ZBI.  The
  // space is safely allocated in our present reckoning so it's disjoint from
  // the data and kernel image memory and from this shim's own image, but as
  // soon as we boot into the new kernel it will be reclaimable memory.
  if (auto result = BootZbi::Load(extra_data_capacity + static_cast<uint32_t>(Trampoline::size()),
                                  kernel_load_address_);
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

  return fit::ok();
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

  if (kernel_load_address_) {
    uintptr_t fixed_first = static_cast<uintptr_t>(kernel_load_address_.value());
    uintptr_t fixed_last = static_cast<uintptr_t>(*kernel_load_address_ + KernelLoadSize() - 1);
    ZX_ASSERT_MSG(fixed_first == *kernel_load_address_, "0x%016" PRIx64 " != 0x%016" PRIx64 " ",
                  static_cast<uint64_t>(fixed_first), *kernel_load_address_);
    ZX_ASSERT(fixed_last == *kernel_load_address_ + KernelLoadSize() - 1);
  }

  if (!trampoline_) {
    // This is a new-style position-independent kernel.  Boot it where it is.
    BootZbi::Boot(argument);
  }

  Log();

  uintptr_t zbi_location =
      reinterpret_cast<uintptr_t>(argument.value_or(DataZbi().storage().data()));
  auto kernel_blob = ktl::span<const ktl::byte>(reinterpret_cast<const ktl::byte*>(KernelImage()),
                                                KernelLoadSize());
  auto zbi_blob = ktl::span<const ktl::byte>(reinterpret_cast<const ktl::byte*>(zbi_location),
                                             DataZbi().size_bytes());
  trampoline_->Boot(
      RelocateTarget(static_cast<uintptr_t>(*kernel_load_address_), kernel_blob),
      RelocateTarget(static_cast<uintptr_t>(data_load_address_.value_or(zbi_location)), zbi_blob),
      KernelEntryAddress());
}

fit::result<TrampolineBoot::Error> TrampolineBoot::Init(InputZbi zbi) {
  auto res = BootZbi::Init(zbi);
  SetKernelAddresses();
  return res;
}

fit::result<TrampolineBoot::Error> TrampolineBoot::Init(InputZbi zbi,
                                                        InputZbi::iterator kernel_item) {
  auto res = BootZbi::Init(zbi, kernel_item);
  SetKernelAddresses();
  return res;
}

void TrampolineBoot::Log() {
  LogAddresses();
  if (trampoline_) {
    LogFixedAddresses();
  }
  LogBoot(KernelEntryAddress());
}

// This output lines up with what BootZbi::LogAddresses() prints.
void TrampolineBoot::LogFixedAddresses() const {
#define ADDR "0x%016" PRIx64
  const uint64_t kernel = kernel_load_address_.value();
  const uint64_t bss = kernel + KernelLoadSize();
  const uint64_t end = kernel + KernelMemorySize();
  debugf("%s: Relocated\n", ProgramName());
  debugf("%s:    Kernel @ [" ADDR ", " ADDR ")\n", ProgramName(), kernel, bss);
  debugf("%s:       BSS @ [" ADDR ", " ADDR ")\n", ProgramName(), bss, end);
  if (data_load_address_) {
    debugf("%s:       ZBI @ [" ADDR ", " ADDR ")\n", ProgramName(), *data_load_address_,
           *data_load_address_ + DataLoadSize());
  }
}
