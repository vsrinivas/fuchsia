// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWREG_PIO_H_
#define HWREG_PIO_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <type_traits>
#include <variant>

#include <hwreg/mmio.h>

namespace hwreg {

// This is used for PIO via MMIO, where a 1-byte port offset is scaled
// to correspond to a 4-byte MMIO address.
using RegisterMmioPio = RegisterMmioScaled<4>;

#if defined(__x86_64__) || defined(__i386__)

// This can be passed to ReadFrom and WriteTo methods.  The RegisterAddr
// object passes uint32_t even though port addresses are only 16 bits.
// This can either be used default-constructed where the RegisterAddr
// contains the full I/O port address, or be constructed with a base
// port address that is added to the value stored in the RegisterAddr.

class RegisterDirectPio {
 public:
  RegisterDirectPio() = default;
  RegisterDirectPio(uint16_t base) : base_(base) {}
  RegisterDirectPio(const RegisterDirectPio& other) : base_(other.base_) {}

  template <typename IntType>
  void Write(IntType value, uint32_t port) const {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    if constexpr (sizeof(value) == sizeof(uint64_t)) {
      Write(static_cast<uint32_t>(value), port);
      Write(static_cast<uint32_t>(value >> 32), port + 1);
    } else {
      auto p = AdjustPort(port);
      // The "a" constraint means the A register, so %al, %ax, or %eax,
      // depending on the type of the value.  The "N" constraint means an
      // 8-bit immediate, so use that if it's constant and fits; otherwise
      // the "d" constraint means the D register, i.e. %dx for uint16_t.
      __asm__ volatile("out %[v], %[p]" : : [v] "a"(value), [p] "Nd"(p));
    }
  }

  template <typename IntType>
  IntType Read(uint32_t port) const {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    IntType value;
    if constexpr (sizeof(value) == sizeof(uint64_t)) {
      auto lo = Read<uint32_t>(port);
      auto hi = Read<uint32_t>(port + 1);
      value = (static_cast<IntType>(hi) << 32) | lo;
    } else {
      auto p = AdjustPort(port);
      // Same operands as above, except "=a" for output to the A register,
      // and the opposite order in the assembly syntax.
      __asm__ volatile("in %[p], %[v]" : [v] "=a"(value) : [p] "Nd"(p));
    }
    return value;
  }

 private:
  const uint16_t base_ = 0;

  uint16_t AdjustPort(uint32_t offset) const {
    auto p = static_cast<uint16_t>(offset);
    ZX_DEBUG_ASSERT(p == offset);
    p += base_;
    ZX_DEBUG_ASSERT(p == base_ + offset);
    return p;
  }
};
static_assert(std::is_copy_constructible_v<RegisterDirectPio>);

// This can be default-constructed or constructed with a uint16_t argument to
// do direct PIO; or constructed with a pointer argument to do PIO via MMIO,
// where a 1-byte port offset is scaled to correspond to a 4-byte MMIO address.
using RegisterPio = std::variant<RegisterDirectPio, RegisterMmioPio>;
static_assert(internal::IsVariant<RegisterPio>);
static_assert(std::is_copy_constructible_v<RegisterPio>);

#else

// Only x86 has direct PIO, so this is always mapped to MMIO.
using RegisterPio = RegisterMmioPio;

#endif

}  // namespace hwreg

#endif  // HWREG_PIO_H_
