// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "relocation.h"

#include <lib/static-pie/static-pie.h>

#include <atomic>
#include <climits>
#include <cstdint>

#include <fbl/span.h>

#include "elf-types.h"

namespace static_pie {

void ApplyRelaRelocs(Program program, fbl::Span<const Elf64RelaEntry> table, uint64_t base) {
  // We require that all entries in the table are `R_RELATIVE` entries.
  for (const Elf64RelaEntry& entry : table) {
    ZX_DEBUG_ASSERT(entry.info.type() == ElfRelocType::kRelative);

    // Patch in the relocation: set the memory value to `base + addend`.
    program.WriteWord(entry.offset, base + entry.addend);
  }
}

void ApplyRelRelocs(Program program, fbl::Span<const Elf64RelEntry> table, uint64_t base) {
  // We require that all entries in the table are `R_RELATIVE` entries.
  for (const Elf64RelEntry& entry : table) {
    ZX_DEBUG_ASSERT(entry.info.type() == ElfRelocType::kRelative);

    // Patch in the relocation: add `base` to memory value.
    program.WriteWord(entry.offset, program.ReadWord(entry.offset) + base);
  }
}

void ApplyRelrRelocs(Program program, fbl::Span<const uint64_t> table, uint64_t base) {
  uint64_t address = 0;
  for (uint64_t value : table) {
    // If the value is an address (low bit is 0), simply patch it in.
    if ((value & 1) == 0) {
      ZX_DEBUG_ASSERT(value != 0);
      program.WriteWord(value, program.ReadWord(value) + base);
      address = value + sizeof(uint64_t);
      continue;
    }

    // Otherwise, the value is a bitmap, indicating which of the next 63 words
    // should be updated.
    uint64_t bitmap = value >> 1;
    uint64_t bitmap_address = address;
    while (bitmap != 0) {
      // Skip over `skip` words that need not be patched.
      uint64_t skip = __builtin_ctzll(bitmap);
      bitmap_address += skip * sizeof(uint64_t);
      bitmap >>= (skip + 1);

      // Patch this word.
      program.WriteWord(bitmap_address, program.ReadWord(bitmap_address) + base);
      bitmap_address += sizeof(uint64_t);
    }

    // Move `address` ahead 63 words.
    constexpr uint64_t bits_per_bitmap = (sizeof(uint64_t) * CHAR_BIT) - 1;
    address += sizeof(uint64_t) * bits_per_bitmap;
  }
}

void ApplyDynamicRelocs(Program program, fbl::Span<const Elf64DynamicEntry> table, uint64_t base) {
  // Locations and sizes of the rel, rela, and relr tables.
  struct RelocationTable {
    uint64_t start = 0;     // Address of the table.
    size_t size_bytes = 0;  // Size of the table, in bytes.

    // Number of R_RELATIVE entries in the table.
    //
    // These are required to be ordered first in the `.rel` and `.rela` table.
    uint64_t num_relative_relocs = 0;
  };
  RelocationTable rel_table{};
  RelocationTable rela_table{};
  RelocationTable relr_table{};

  // Process entries in the ".dynamic" table.
  for (size_t i = 0; i < table.size() && table[i].tag != DynamicArrayTag::kNull; i++) {
    switch (table[i].tag) {
      // Rela table.
      case DynamicArrayTag::kRela:
        rela_table.start = table[i].value;
        break;
      case DynamicArrayTag::kRelaSize:
        rela_table.size_bytes = table[i].value;
        break;
      case DynamicArrayTag::kRelaCount:
        rela_table.num_relative_relocs = table[i].value;
        break;
      case DynamicArrayTag::kRelaEntrySize:
        ZX_ASSERT(table[i].value == sizeof(Elf64RelaEntry));
        break;

      // Rel table.
      case DynamicArrayTag::kRel:
        rel_table.start = table[i].value;
        break;
      case DynamicArrayTag::kRelSize:
        rel_table.size_bytes = table[i].value;
        break;
      case DynamicArrayTag::kRelCount:
        rel_table.num_relative_relocs = table[i].value;
        break;
      case DynamicArrayTag::kRelEntrySize:
        ZX_ASSERT(table[i].value != sizeof(Elf64RelEntry));
        break;

      // Relr table.
      case DynamicArrayTag::kRelr:
        relr_table.start = table[i].value;
        break;
      case DynamicArrayTag::kRelrSize:
        relr_table.size_bytes = table[i].value;
        break;
      case DynamicArrayTag::kRelrEntrySize:
        ZX_ASSERT(table[i].value == sizeof(uint64_t));
        break;

      default:
        break;
    }
  }

  // Apply any relocations.
  {
    fbl::Span<const uint64_t> table =
        program.MapRegion<const uint64_t>(relr_table.start, relr_table.size_bytes);
    ApplyRelrRelocs(program, table, base);
  }
  {
    fbl::Span<const Elf64RelaEntry> table =
        program.MapRegion<const Elf64RelaEntry>(rela_table.start, rela_table.size_bytes);
    // Only the first `num_relative_relocs` will be R_RELATIVE entries.
    ApplyRelaRelocs(program, table.subspan(0, rela_table.num_relative_relocs), base);
  }
  {
    fbl::Span<const Elf64RelEntry> table =
        program.MapRegion<const Elf64RelEntry>(rel_table.start, rel_table.size_bytes);
    // Only the first `num_relative_relocs` will be R_RELATIVE entries.
    ApplyRelRelocs(program, table.subspan(0, rel_table.num_relative_relocs), base);
  }
}

void ApplyDynamicRelocations(const Elf64DynamicEntry* dynamic_table, uintptr_t load_address) {
  // Apply relocations.
  fbl::Span<std::byte> data(reinterpret_cast<std::byte*>(load_address), SIZE_MAX);
  ApplyDynamicRelocs(Program{data}, fbl::Span<const Elf64DynamicEntry>(dynamic_table, SIZE_MAX),
                     load_address);

  // Compiler barrier. Ensure stores are committed prior to return.
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

}  // namespace static_pie
