// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

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

  // The 32-bit base address is split across three fields.
  constexpr uint32_t base() const { return base_lo16() | (base_mid8() << 16) | (base_hi8() << 24); }

  constexpr auto& set_base(uint32_t base) {
    set_base_lo16(base & 0xffff);
    set_base_mid8((base >> 16) & 0xff);
    set_base_hi8(base >> 24);
    return *this;
  }

  // A segment's limit is the the size of the memory range starting at the
  // base address, minus one.  The 20-bit limit field is split across two
  // fields, plus the granularity flag determines whether it's scaled by 12
  // bits.  The limit() and set_limit() accessors take a whole unscaled 32-bit
  // value.  If the low bits are 0xfff, then it's stored as a scaled value that
  // can represent the full 32-bit size (with 4k granularity).  Otherwise, it's
  // stored unscaled and only has 20 bits (with byte granularity: max 1MB - 1).
  constexpr auto limit() const {
    return (limit_lo16() | (limit_hi4() << 16)) << (granularity() ? 12 : 0);
  }

  constexpr auto& set_limit(uint32_t value) {
    // There are only 20 bits for the limit, so a byte-granularity limit can
    // only go up to 2MB - 1 byte.  If the granularity flag is set, then the
    // limit is scaled up by 4KB and the low 12 bits are treated as all ones.
    // So if the low bits of the intended 32-bit limit are all ones, then the
    // limit can go up to 4GB - 1.  Otherwise, it can only go up to 2MB - 1.
    if ((value & 0xfff) == 0xfff) {
      set_granularity(true);
      value >>= 12;
    } else {
      set_granularity(false);
    }
    set_limit_lo16(value & 0xffff);
    set_limit_hi4(value >> 16);
    return *this;
  }

  // Non-system segments are most often "flat", i.e. base 0 and maximum size.
  constexpr auto& make_flat() {
    set_present(true);
    set_system(arch::Desc32::SegmentSystem::NONSYSTEM);
    set_addr32(true);
    set_base(0);
    set_limit(~uint32_t{0});
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

  constexpr uint64_t base() const { return (static_cast<uint64_t>(base_hi32) << 32) | Desc32::base(); }

  constexpr auto& set_base(uint64_t base) {
    base_hi32 = static_cast<uint32_t>(base >> 32);
    return Desc32::set_base(static_cast<uint32_t>(base));
  }
};

}  // namespace arch
