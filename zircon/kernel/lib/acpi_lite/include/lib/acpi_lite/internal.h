// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_INTERNAL_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

namespace acpi_lite {

// Endian conversion macros.
//
// Convert between host and big-endian formats.
#ifndef __BYTE_ORDER__
#error "Compiler does not provide __BYTE_ORDER__"
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
inline constexpr uint32_t HostToBe32(uint32_t x) { return x; }
inline constexpr uint32_t BeToHost32(uint32_t x) { return x; }
#else
inline constexpr uint32_t HostToBe32(uint32_t x) { return __builtin_bswap32(x); }
inline constexpr uint32_t BeToHost32(uint32_t x) { return __builtin_bswap32(x); }
#endif

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_INTERNAL_H_
