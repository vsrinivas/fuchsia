// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_NOP_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_NOP_H_

#include <lib/arch/arm64/nop.h>
#include <lib/arch/x86/nop.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <cstddef>

namespace arch {

// Fills a range of instructions with archictecture-appropriate `nop`s.
// The function is templatized on a 'nop traits' struct that is expected to
// have the following static members:
//
// * `static constexpr std::array<ktl::span<const INT_TYPE, NUM>> kNopPatterns`
//   giving an array of the arch-specific `nop` patterns in descending order of
//   of size, where INT_TYPE is of instruction width. Some architectures (such
//   x86 have several different `nop` instructions with different encoding
//   lengths. We require that the array is in descending order so we may use
//   the longer encodings first to minimize the total number of instructions
//   emitted, and drop down to smaller encodings when the longer instructions
//   no longer fit.
//
// The templating - with default parameters - here allows for the user to
// #include a single arch-agnostic header with the appropriate arch-specific
// logic - and also allows convenient testing of the specifics in each case.
//
template <typename NopTraits
#if defined(__aarch64__)
          = Arm64NopTraits
#elif defined(__x86_64__)
          = X86NopTraits
#endif
          >
void NopFill(cpp20::span<std::byte> instructions) {
  static_assert(!NopTraits::kNopPatterns.empty());
  static_assert(!NopTraits::kNopPatterns[0].empty());
  static constexpr size_t kInsnAlignment = sizeof(NopTraits::kNopPatterns[0][0]);

  // Ensure that the final and smallest `nop` instruction length can always be
  // used for any range that meets our alignment requirements.
  static_assert(NopTraits::kNopPatterns.back().size_bytes() % kInsnAlignment == 0);

  ZX_ASSERT(reinterpret_cast<uintptr_t>(instructions.data()) % kInsnAlignment == 0);
  ZX_ASSERT(instructions.size() % kInsnAlignment == 0);

  for (const auto& nop : NopTraits::kNopPatterns) {
    while (nop.size_bytes() <= instructions.size()) {
      memcpy(instructions.data(), nop.data(), nop.size_bytes());
      instructions = instructions.subspan(nop.size_bytes());
    }
    if (instructions.empty()) {
      break;
    }
  }
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_NOP_H_
