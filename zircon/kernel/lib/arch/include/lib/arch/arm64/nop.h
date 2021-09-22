// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_NOP_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_NOP_H_

#include <lib/stdcompat/span.h>

#include <array>

namespace arch {

// Encodes information on the arm64 `nop` instruction.
// See //zircon/kernel/lib/arch/include/lib/arch/nop.h for expectations for
// the static members of this struct.
struct Arm64NopTraits {
  // [arm/v8]: C6.2.203  NOP.
  static constexpr uint32_t kNop[] = {0xd503201f};

  static constexpr std::array<cpp20::span<const uint32_t>, 1> kNopPatterns = {{kNop}};
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_NOP_H_
