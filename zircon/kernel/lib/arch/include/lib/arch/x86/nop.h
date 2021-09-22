// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_NOP_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_NOP_H_

#include <lib/stdcompat/span.h>

#include <array>
#include <cstddef>

namespace arch {

// Encodes information on x86 `nop` instructions.
// See //zircon/kernel/lib/arch/include/lib/arch/nop.h for expectations for
// the static members of this struct.
struct X86NopTraits {
  // [intel/vol3]: Table 4-12.  Recommended Multi-Byte Sequence of NOP Instruction.
  // [amd/sog/17h]: 2.8.3.1   Encoding Padding for Loop Alignment.
  // clang-format off
  static constexpr uint8_t kNop1[]  = {0x90};
  static constexpr uint8_t kNop2[]  = {0x66, 0x90};
  static constexpr uint8_t kNop3[]  = {0x0f, 0x1f, 0x00};
  static constexpr uint8_t kNop4[]  = {0x0f, 0x1f, 0x40, 0x00};
  static constexpr uint8_t kNop5[]  = {0x0f, 0x1f, 0x44, 0x00, 0x00};
  static constexpr uint8_t kNop6[]  = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};
  static constexpr uint8_t kNop7[]  = {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop8[]  = {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop9[]  = {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop10[] = {0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop11[] = {0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop12[] = {0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop13[] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop14[] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop15[] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
  // clang-format on

  // Expected to be in descending order of size.
  static constexpr std::array<cpp20::span<const uint8_t>, 15> kNopPatterns = {{
      {kNop15},
      {kNop14},
      {kNop13},
      {kNop12},
      {kNop11},
      {kNop10},
      {kNop9},
      {kNop8},
      {kNop7},
      {kNop6},
      {kNop5},
      {kNop4},
      {kNop3},
      {kNop2},
      {kNop1},
  }};
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_NOP_H_
