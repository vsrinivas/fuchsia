// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_

#include <hwreg/bitfields.h>

namespace arch {

// This represents the 32-bit descriptor format in the GDT or LDT.
// The 64-bit descriptor format below extends this.  The low 32 bits
// are laid out the same in both formats.
struct Desc32 {
  // These raw fields are normally accessed via the accessors defined below.
  uint32_t limit_base_lo16;
  uint32_t flags_base_hi16;

  enum SegmentSystem : uint32_t {
    SYSTEM = 0,
    NONSYSTEM = 1,
  };

  enum SegmentType : uint32_t {
    DATA_RO = 0b000,
    DATA_RW = 0b001,
    DATA_RO_DOWN = 0b010,
    DATA_RW_DOWN = 0b011,
    CODE_XO = 0b100,
    CODE_RX = 0b101,
    CODE_XO_CONFORMING = 0b110,
    CODE_RX_CONFORMING = 0b111,
  };

  DEF_SUBFIELD(limit_base_lo16, 15, 0, limit_lo16);
  DEF_SUBFIELD(limit_base_lo16, 31, 16, base_lo16);

  DEF_SUBFIELD(flags_base_hi16, 7, 0, base_mid8);
  DEF_ENUM_SUBFIELD(flags_base_hi16, SegmentType, 11, 9, type);
  DEF_SUBBIT(flags_base_hi16, 8, accessed);
  DEF_ENUM_SUBFIELD(flags_base_hi16, SegmentSystem, 12, 12, system);
  DEF_SUBFIELD(flags_base_hi16, 14, 13, dpl);
  DEF_SUBBIT(flags_base_hi16, 15, present);
  DEF_SUBFIELD(flags_base_hi16, 19, 16, limit_hi4);
  DEF_SUBBIT(flags_base_hi16, 20, avl);
  DEF_SUBBIT(flags_base_hi16, 21, long_mode);
  DEF_SUBBIT(flags_base_hi16, 22, addr32);
  DEF_SUBBIT(flags_base_hi16, 23, granularity);
  DEF_SUBFIELD(flags_base_hi16, 31, 24, base_hi8);

  // Get/set the 32-bit base address, splitting/combining its three fields.
  constexpr uint32_t base() const { return base_lo16() | (base_mid8() << 16) | (base_hi8() << 24); }

  constexpr Desc32& set_base(uint32_t base) {
    set_base_lo16(base & 0xffff);
    set_base_mid8((base >> 16) & 0xff);
    set_base_hi8(base >> 24);
    return *this;
  }

  // Get/set the 20-bit limit, splitting/combining its two fields.
  //
  // The interpretation of the 20-bit limit depends on the granularity bit.
  // See `ScaledLimit` and `SetScaledLimit` for versions that avoid callers
  // from having to scale manually.
  constexpr uint32_t limit() const { return limit_lo16() | (limit_hi4() << 16); }
  constexpr Desc32& set_limit(uint32_t value) {
    set_limit_lo16(value & 0xffff);
    set_limit_hi4(value >> 16);
    return *this;
  }

  // Get/set the 20-bit limit, also attempting to set/use the granuality bit
  // as appropriate.
  //
  // A segment's limit is the the size of the memory range starting at the
  // base address, minus one. The 20-bit limit can then be scaled according
  // to the granuality bit, which multiplies the value by 12 bits (4096).
  uint32_t ScaledLimit() const { return limit() << (granularity() ? 12 : 0); }
  constexpr Desc32& SetScaledLimit(uint32_t value) {
    if ((value & 0xfff) == 0xfff) {
      set_granularity(1);
      set_limit(value >> 12);
    } else {
      set_granularity(0);
      set_limit(value);
    }
    return *this;
  }

  // Set fields to make this a 32-bit "flat" code/data segment.
  //
  // Such segments span the entire 32-bit address space, starting from 0.
  constexpr Desc32& MakeFlat() {
    set_present(true);
    set_system(arch::Desc32::SegmentSystem::NONSYSTEM);
    set_addr32(true);
    set_base(0);
    SetScaledLimit(UINT32_MAX);
    return *this;
  }
};

// This represents the 64-bit descriptor format in the GDT or LDT.  It's the
// same as the 32-bit format except for having a 64-bit base address.  It
// inherits all the other fields from Desc32 and overrides the base accessors
// with a 64-bit version.
struct Desc64 : public Desc32 {
  uint32_t base_hi32;
  uint32_t rsvdz;

  constexpr uint64_t base() const {
    return (static_cast<uint64_t>(base_hi32) << 32) | Desc32::base();
  }

  constexpr auto& set_base(uint64_t base) {
    base_hi32 = static_cast<uint32_t>(base >> 32);
    return Desc32::set_base(static_cast<uint32_t>(base));
  }
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_
