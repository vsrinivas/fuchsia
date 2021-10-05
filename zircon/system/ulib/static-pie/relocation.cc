// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "relocation.h"

#include <lib/static-pie/static-pie.h>
#include <lib/stdcompat/span.h>

#include <atomic>
#include <climits>
#include <cstdint>

#include "elf-types.h"

namespace static_pie {

// Apply a fixup function to the word at `addr`.
//
// We assume that callers only want to convert LinkTimeAddr's in the
// program to RunTimeAddr's: hence, `fixup` is given a LinkTimeAddr and
// should return a RunTimeAddr.
template <typename F>
void ApplyFixup(const Program& program, LinkTimeAddr addr, F&& fixup) {
  LinkTimeAddr orig_word = LinkTimeAddr(program.ReadWord(addr));
  RunTimeAddr fixed_word = fixup(orig_word);
  program.WriteWord(addr, fixed_word.value());
}

void ApplyRelaRelocs(const Program& program, cpp20::span<const Elf64RelaEntry> table) {
  // We require that all entries in the table are `R_RELATIVE` entries.
  for (const Elf64RelaEntry& entry : table) {
    ZX_DEBUG_ASSERT(entry.info.type() == ElfRelocType::kRelative);

    // `entry.addend` contains a link-time address. We simply convert it
    // to a run-time address and write it into the program.
    ApplyFixup(program, entry.offset, [&](LinkTimeAddr /*ignored*/) {
      return program.ToRunTimeAddr(LinkTimeAddr{entry.addend});
    });
  }
}

void ApplyRelRelocs(const Program& program, cpp20::span<const Elf64RelEntry> table) {
  // We require that all entries in the table are `R_RELATIVE` entries.
  for (const Elf64RelEntry& entry : table) {
    ZX_DEBUG_ASSERT(entry.info.type() == ElfRelocType::kRelative);

    // `entry.offset` points to a link-time address. We convert it to
    // a run-time address.
    ApplyFixup(program, entry.offset,
               [&](LinkTimeAddr addr) { return program.ToRunTimeAddr(addr); });
  }
}

void ApplyRelrRelocs(const Program& program, cpp20::span<const uint64_t> table) {
  LinkTimeAddr address = LinkTimeAddr(0);

  for (uint64_t value : table) {
    // If the value is an address (low bit is 0), simply patch it in.
    if ((value & 1) == 0) {
      ZX_DEBUG_ASSERT(value != 0);
      address = LinkTimeAddr(value);

      ApplyFixup(program, address,
                 [&](LinkTimeAddr input) { return program.ToRunTimeAddr(input); });
      address += sizeof(uint64_t);

      continue;
    }

    // Otherwise, the value is a bitmap, indicating which of the next 63 words
    // should be updated.
    uint64_t bitmap = value >> 1;
    LinkTimeAddr bitmap_address = address;
    while (bitmap != 0) {
      // Skip over `skip` words that need not be patched.
      uint64_t skip = __builtin_ctzll(bitmap);
      bitmap_address += skip * sizeof(uint64_t);
      bitmap >>= (skip + 1);

      // Patch this word.
      ApplyFixup(program, bitmap_address,
                 [&](LinkTimeAddr input) { return program.ToRunTimeAddr(input); });
      bitmap_address += sizeof(uint64_t);
    }

    // Move `address` ahead 63 words.
    constexpr uint64_t bits_per_bitmap = (sizeof(uint64_t) * CHAR_BIT) - 1;
    address += sizeof(uint64_t) * bits_per_bitmap;
  }
}

void ApplyDynamicRelocs(Program& program, cpp20::span<const Elf64DynamicEntry> table) {
  // Locations and sizes of the rel, rela, and relr tables.
  struct RelocationTable {
    LinkTimeAddr start = LinkTimeAddr(0);  // Address of the table.
    size_t size_bytes = 0;                 // Size of the table, in bytes.

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
        rela_table.start = LinkTimeAddr(table[i].value);
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
        rel_table.start = LinkTimeAddr(table[i].value);
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
        relr_table.start = LinkTimeAddr(table[i].value);
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
    cpp20::span<const uint64_t> relr_span =
        program.MapRegion<const uint64_t>(relr_table.start, relr_table.size_bytes);
    ApplyRelrRelocs(program, relr_span);
  }
  {
    cpp20::span<const Elf64RelaEntry> rela_span =
        program.MapRegion<const Elf64RelaEntry>(rela_table.start, rela_table.size_bytes);
    // Only the first `num_relative_relocs` will be R_RELATIVE entries.
    ApplyRelaRelocs(program, rela_span.subspan(0, rela_table.num_relative_relocs));
  }
  {
    cpp20::span<const Elf64RelEntry> rel_span =
        program.MapRegion<const Elf64RelEntry>(rel_table.start, rel_table.size_bytes);
    // Only the first `num_relative_relocs` will be R_RELATIVE entries.
    ApplyRelRelocs(program, rel_span.subspan(0, rel_table.num_relative_relocs));
  }
}

}  // namespace static_pie
