// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_H_

#include <lib/zircon-internal/align.h>
#include <stddef.h>
#include <zircon/assert.h>

#include <cstdint>
#include <string_view>

namespace devicetree {
namespace internal {

using ByteView = std::basic_string_view<uint8_t>;

constexpr uint32_t kMagic = 0xd00dfeed;
// The structure block tokens, named as in the spec for clarity.
constexpr uint32_t FDT_BEGIN_NODE = 0x00000001;
constexpr uint32_t FDT_END_NODE = 0x00000002;
constexpr uint32_t FDT_PROP = 0x00000003;
constexpr uint32_t FDT_NOP = 0x00000004;
constexpr uint32_t FDT_END = 0x00000009;

struct ReadBigEndianUint32Result {
  uint32_t value;
  ByteView tail;
};

inline ReadBigEndianUint32Result ReadBigEndianUint32(ByteView bytes) {
  ZX_ASSERT(bytes.size() >= sizeof(uint32_t));
  return {
      (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
          (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]),
      bytes.substr(sizeof(uint32_t)),
  };
}

// Structure block tokens are 4-byte aligned.
inline size_t StructBlockAlign(size_t x) { return ZX_ALIGN(x, sizeof(uint32_t)); }

}  // namespace internal
}  // namespace devicetree

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_H_
