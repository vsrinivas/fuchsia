// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_

#include <stddef.h>

#include <hwreg/bitfields.h>

namespace arch {

// This represents the 32-bit descriptor format in the GDT or LDT.
struct Desc32 {
  // These raw fields are normally accessed via the accessors defined below.
  alignas(uint64_t) uint32_t limit_base_lo16;
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

  // Word 0
  DEF_SUBFIELD(limit_base_lo16, 15, 0, limit_lo16);
  DEF_SUBFIELD(limit_base_lo16, 31, 16, base_lo16);

  // Word 1
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

  // Set fields to make this a 64-bit code segment.
  constexpr Desc32& MakeCode64() {
    set_type(arch::Desc32::CODE_RX);
    set_system(arch::Desc32::SegmentSystem::NONSYSTEM);
    set_present(true);
    set_addr32(false);
    set_base(0);
    set_long_mode(true);
    SetScaledLimit(UINT32_MAX);
    return *this;
  }
};
static_assert(sizeof(Desc32) == 8);

// A 64-bit system segment.
//
// These descriptors are used in 64-bit mode for system segments, call gates,
// interrupt gates, and trap gates.
//
// Code and data segment descriptors continue to use the 32-bit descriptor
// Desc32 format above.
//
// When used in the GDT or LDT, these 64-bit descriptors occupy two slots
// in the table.
//
// [amd/vol2]: Section 4.8.3. System Descriptors
// [intel/vol3]: Figure 7-4. Format of TSS and LDT Descriptors in 64-bit Mode
struct SystemSegmentDesc64 {
  uint32_t raw[4];

  enum class SegmentType {
    LDT = 0b0010,
    TSS_AVAILABLE = 0b1001,
    TSS_BUSY = 0b1011,
    CALL_GATE = 0b1100,
    INTERRUPT_GATE = 0b1110,
    TRAP_GATE = 0b1111,
  };

  // Word 0
  DEF_SUBFIELD(raw[0], 15, 0, limit_15_0);
  DEF_SUBFIELD(raw[0], 31, 16, base_15_0);

  // Word 1
  DEF_SUBFIELD(raw[1], 7, 0, base_23_16);
  DEF_ENUM_SUBFIELD(raw[1], SegmentType, 11, 8, type);
  // Bit 12 of raw[1] set to 0.
  DEF_SUBFIELD(raw[1], 14, 13, dpl);
  DEF_SUBBIT(raw[1], 15, present);
  DEF_SUBFIELD(raw[1], 19, 16, limit_19_16);
  DEF_SUBBIT(raw[1], 20, avl);
  // Bits [22:21] of raw[1] are reserved.
  DEF_SUBBIT(raw[1], 23, granularity);
  DEF_SUBFIELD(raw[1], 31, 24, base_31_24);

  // Word 2
  DEF_SUBFIELD(raw[2], 31, 0, base_63_32);

  // Word 3
  // Bits [31:0] of raw[3] reserved.

  // Get/set the base address, which is scattered amongst various fields
  // above.
  constexpr uint64_t base() const {
    return (static_cast<uint64_t>(base_63_32()) << 32) | (base_31_24() << 24) |
           (base_23_16() << 16) | base_15_0();
  }
  constexpr SystemSegmentDesc64& set_base(uint64_t base) {
    return set_base_63_32(static_cast<uint32_t>(base >> 32))
        .set_base_31_24((base >> 24) & 0xff)
        .set_base_23_16((base >> 16) & 0xff)
        .set_base_15_0(base & 0xffff);
  }

  // Get/set the limit, which is scattered amongst the various fields above.
  constexpr uint64_t limit() const { return (limit_19_16() << 16) | limit_15_0(); }
  constexpr SystemSegmentDesc64& set_limit(uint64_t limit) {
    return set_limit_19_16((limit >> 16) & 0xff).set_limit_15_0(limit & 0xffff);
  }
};
static_assert(sizeof(SystemSegmentDesc64) == 16);

// Defines a type representing a segment selector.
struct SegmentSelector {
  uint16_t raw;

  DEF_SUBFIELD(raw, 1, 0, rpl);     // Requestor privilege level
  DEF_SUBBIT(raw, 2, is_ldt);       // If 0, a GDT entry. If 1, a LDT entry.
  DEF_SUBFIELD(raw, 15, 3, index);  // Index into the GDT/LDT.

  // Create a selector given an GDT entry's index.
  static constexpr SegmentSelector FromGdtIndex(uint16_t index) {
    return SegmentSelector{}.set_index(index).set_is_ldt(0).set_rpl(0);
  }
};

// Pointer/limit to the system GDT and IDT.
//
// If user mode alignment checks are enabled the struct needs to be aligned
// such that (ptr % 4 == 2), which can be done using `AlignedGdtRegister64`
// below. Privileged mode users or users with alignment checks disabled need
// not worry. (c.f., [intel/vol3] Section 3.5.1 Segment Descriptor Tables)
//
// [intel/vol3]: Figure 2-6. Memory Management Registers
// [amd/vol2]: Figure 4-8. GDTR and IDTR Format-Long Mode.
struct GdtRegister64 {
  uint16_t limit;  // Size of the GDT in bytes, minus one.
  uint64_t base;   // Pointer to the GDT.
} __PACKED __ALIGNED(2);
static_assert(sizeof(GdtRegister64) == 10);

// Wrapper around GdtRegister64 to ensure the inner GdtRegister64 is correctly
// aligned as described above.
struct AlignedGdtRegister64 {
  uint8_t padding[6];
  GdtRegister64 reg;

  AlignedGdtRegister64() = default;
  explicit AlignedGdtRegister64(GdtRegister64 reg) : reg(reg) {}
} __PACKED __ALIGNED(8);
static_assert(offsetof(AlignedGdtRegister64, reg) % 4 == 2);

// x86-64 Task State Segment.
//
// In 64-bit mode, the system needs at least one TSS. It is used to store
// stack pointers for various privilege levels, stack pointers for various
// interrupt handlers, and I/O port permissions.
//
// [amd/vol2]: Figure 12-8. Long Mode TSS Format
// [intel/vol3]: Figure 7-11. 64-Bit TSS Format
struct TaskStateSegment64 {
  uint32_t reserved0;

  // Stack pointers for various privilege levels.
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;

  uint32_t reserved1;
  uint32_t reserved2;

  // Interrupt stack table pointers.
  static constexpr int kNumInterruptStackTables = 7;
  uint64_t ist[kNumInterruptStackTables];

  uint32_t reserved3;
  uint32_t reserved4;
  uint16_t reserved5;

  // 16-bit offset of the I/O port permission map from the base of this structure.
  //
  // The bitmap will typically directly follow this structure directly, but the
  // `io_port_bitmap_base` allows for some other data to come prior to the bitmap.
  //
  // Access is granted for an I/O operation if all bits associated with the
  // read/write are clear. For example, a 2-byte write to port 0x80 will
  // require bits 0x80 and 0x81 to be clear.
  //
  // The CPU may read up to 1 byte past the limit specified, so an additional
  // padding byte of '0xff' should follow the bitmap. (See [amd/vol2] Section
  // 12.2.4, "I/O-Permission Bitmap").
  static constexpr uint32_t kIoMaxBitmapBits = 65536;
  uint16_t io_port_bitmap_base;
} __PACKED __ALIGNED(4);

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_H_
