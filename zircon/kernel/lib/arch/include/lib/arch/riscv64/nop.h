// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_NOP_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_NOP_H_

#include <array>

#include <fbl/span.h>

namespace arch {

// Encodes information on the riscv64 `nop` instruction.
// See //zircon/kernel/lib/arch/include/lib/arch/nop.h for expectations for
// the static members of this struct.
struct Riscv64NopTraits {
  static constexpr uint32_t kNop[] = {0x1};

  static constexpr std::array<fbl::Span<const uint32_t>, 1> kNopPatterns = {{kNop}};
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_NOP_H_
