// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "relocation.h"

#include <lib/stdcompat/span.h>

#include <gtest/gtest.h>

namespace static_pie {
namespace {

TEST(ApplyRelRelocs, EmptyTable) {
  Program program{cpp20::span<std::byte>{}, LinkTimeAddr(0), RunTimeAddr(0)};
  ApplyRelRelocs(program, {});
}

TEST(ApplyRelRelocs, ApplyRelocs) {
  uint64_t program[] = {
      0x00000000'00000000, 0x11111111'11111111, 0x22222222'22222222,
      0x33333333'33333333, 0x44444444'44444444,
  };

  // Apply two relocs, at index 1 and 3.
  constexpr Elf64RelEntry entries[] = {
      {LinkTimeAddr(8), Elf64RelInfo::OfType(ElfRelocType::kRelative)},
      {LinkTimeAddr(24), Elf64RelInfo::OfType(ElfRelocType::kRelative)},
  };
  ApplyRelRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program)), LinkTimeAddr(0),
                         RunTimeAddr{0xaaaaaaaa'aaaaaaaa}),
                 entries);

  // Ensure that the values are correct.
  EXPECT_EQ(program[0], 0x00000000'00000000u);  // no change
  EXPECT_EQ(program[1], 0xbbbbbbbb'bbbbbbbbu);  // updated
  EXPECT_EQ(program[2], 0x22222222'22222222u);  // no change
  EXPECT_EQ(program[3], 0xdddddddd'ddddddddu);  // updated
  EXPECT_EQ(program[4], 0x44444444'44444444u);  // no change
}

TEST(ApplyRelaRelocs, EmptyTable) {
  Program program{cpp20::span<std::byte>{}, LinkTimeAddr(0), RunTimeAddr(0)};
  ApplyRelaRelocs(program, {});
}

TEST(ApplyRelaRelocs, ApplyRelocs) {
  uint64_t program[] = {
      0x00000000'00000000, 0xaeaeaeae'aeaeaeae, 0x22222222'22222222,
      0xeaeaeaea'eaeaeaea, 0x44444444'44444444,
  };

  // Apply two relocs, at index 1 and 3.
  constexpr Elf64RelaEntry entries[] = {
      {LinkTimeAddr(8), Elf64RelInfo::OfType(ElfRelocType::kRelative), 0x11111111'11111111},
      {LinkTimeAddr(24), Elf64RelInfo::OfType(ElfRelocType::kRelative), 0x33333333'33333333}};
  ApplyRelaRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program)), LinkTimeAddr(0),
                          RunTimeAddr(0xaaaaaaaa'aaaaaaaa)),
                  entries);

  // Ensure that the values are correct.
  EXPECT_EQ(program[0], 0x00000000'00000000u);  // no change
  EXPECT_EQ(program[1], 0xbbbbbbbb'bbbbbbbbu);  // updated
  EXPECT_EQ(program[2], 0x22222222'22222222u);  // no change
  EXPECT_EQ(program[3], 0xdddddddd'ddddddddu);  // updated
  EXPECT_EQ(program[4], 0x44444444'44444444u);  // no change
}

TEST(ApplyRelrRelocs, EmptyTable) {
  Program program{cpp20::span<std::byte>{}, LinkTimeAddr(0), RunTimeAddr(0)};
  ApplyRelrRelocs(program, {});
}

TEST(ApplyRelrRelocs, SingleReloc) {
  // Update a single entry in the program.
  uint64_t program[] = {
      0x00000000'00000000,
      0x00000000'00000001,
  };
  constexpr uint64_t relocs[] = {
      0x00000000'00000008,
  };
  ApplyRelrRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program)), LinkTimeAddr(0),
                          RunTimeAddr(0xffffffff'00000000)),
                  relocs);
  EXPECT_EQ(program[1], 0xffffffff'00000001u);
}

TEST(ApplyRelrRelocs, NoBitmaps) {
  // Update 3 entries in the program, not using any bitmaps.
  uint64_t program[] = {
      0x00000000'00000000, 0x00000000'00000001, 0x00000000'00000002,
      0x00000000'00000003, 0x00000000'00000004, 0x00000000'00000005,
  };
  constexpr uint64_t relocs[] = {
      0x00000000'00000008,  // update index 1.
      0x00000000'00000018,  // update index 3.
      0x00000000'00000028,  // update index 5.
  };
  ApplyRelrRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program)), LinkTimeAddr(0),
                          RunTimeAddr(0xffffffff'00000000)),
                  relocs);

  EXPECT_EQ(program[0], 0x00000000'00000000u);
  EXPECT_EQ(program[1], 0xffffffff'00000001u);
  EXPECT_EQ(program[2], 0x00000000'00000002u);
  EXPECT_EQ(program[3], 0xffffffff'00000003u);
  EXPECT_EQ(program[4], 0x00000000'00000004u);
  EXPECT_EQ(program[5], 0xffffffff'00000005u);
}

TEST(ApplyRelrRelocs, SingleBitmap) {
  // Update 3 entries in the program, using a bitmap.
  uint64_t program[] = {
      0x00000000'00000000, 0x00000000'00000001, 0x00000000'00000002,
      0x00000000'00000003, 0x00000000'00000004, 0x00000000'00000005,
  };
  constexpr uint64_t relocs[] = {
      0x00000000'00000008,  // update index 1.
      0x00000000'00000015,  // 0b10101 ; update index {prev + 2, prev + 4}.
  };
  ApplyRelrRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program)), LinkTimeAddr(0),
                          RunTimeAddr(0xffffffff'00000000)),
                  relocs);
  EXPECT_EQ(program[0], 0x00000000'00000000u);
  EXPECT_EQ(program[1], 0xffffffff'00000001u);
  EXPECT_EQ(program[2], 0x00000000'00000002u);
  EXPECT_EQ(program[3], 0xffffffff'00000003u);
  EXPECT_EQ(program[4], 0x00000000'00000004u);
  EXPECT_EQ(program[5], 0xffffffff'00000005u);
}

TEST(ApplyRelrRelocs, MultipleBitmaps) {
  // Create a large program.
  constexpr int kSize = 256;
  std::array<uint64_t, kSize> program;
  for (uint64_t i = 0; i < kSize; i++) {
    program[i] = i;
  }

  // Start at offset 1, and then update every second word.
  constexpr uint64_t relocs[] = {
      0x00000000'00000008,  // update index 1.
      0x55555555'55555555,  // 0b0101010 ... 101010101
      0xaaaaaaaa'aaaaaaab,  // 0b1010101 ... 010101011
  };
  ApplyRelrRelocs(Program(cpp20::as_writable_bytes(cpp20::span(program.data(), program.size())),
                          LinkTimeAddr(0), RunTimeAddr(0xffffffff'00000000)),
                  relocs);

  // Expect the first 1 + 63 + 63 odd offsets to be updated, while the rest remain unchanged.
  for (uint64_t i = 0; i < kSize; i++) {
    if (i % 2 == 1 && i <= 1 + 63 + 63) {
      EXPECT_EQ(program[i], i + 0xffffffff'00000000u);
    } else {
      EXPECT_EQ(program[i], i);
    }
  }
}

TEST(ApplyDynamicRelocs, EmptyTable) {
  Program program{cpp20::span<std::byte>{}, LinkTimeAddr(0), RunTimeAddr(0)};
  ApplyDynamicRelocs(program, {});
}

// BinaryWriter allows joining raw structures into a contiguous region of memory.
class BinaryWriter {
 public:
  BinaryWriter() = default;
  explicit BinaryWriter(LinkTimeAddr addr) : link_addr_(addr) {}

  // Append the given value onto the program.
  //
  // Return the LinkTimeAddr that the data was written to.
  template <typename T>
  LinkTimeAddr Write(T value) {
    uint64_t offset = data_.size();
    const std::byte* ptr = reinterpret_cast<std::byte*>(&value);
    data_.insert(data_.end(), ptr, ptr + sizeof(value));
    return link_addr_ + offset;
  }

  cpp20::span<std::byte> data() { return {data_.data(), data_.size()}; }

 private:
  std::vector<std::byte> data_;
  LinkTimeAddr link_addr_{};
};

TEST(ApplyDynamicRelocs, OneOfEach) {
  // Create a fake "ELF program" with a rela, rel, and relr sections.
  BinaryWriter writer;

  // Write out some program values.
  writer.Write(uint64_t{0});
  LinkTimeAddr offset1 = writer.Write(uint64_t{1});
  LinkTimeAddr offset2 = writer.Write(uint64_t{2});
  LinkTimeAddr offset3 = writer.Write(uint64_t{3});

  // Write out a single rel entry (patching offset1), rela entry (patching offset2), and relr entry
  // (patching offset3).
  LinkTimeAddr rel_table = writer.Write(Elf64RelEntry{
      .offset = offset1,
      .info = Elf64RelInfo::OfType(ElfRelocType::kRelative),
  });
  LinkTimeAddr rela_table = writer.Write(Elf64RelaEntry{
      .offset = offset2, .info = Elf64RelInfo::OfType(ElfRelocType::kRelative), .addend = 2});
  LinkTimeAddr relr_table = writer.Write(offset3);

  // Generate a dynamic table.
  Elf64DynamicEntry dynamic[] = {
      {.tag = DynamicArrayTag::kRel, .value = rel_table.value()},
      {.tag = DynamicArrayTag::kRelSize, .value = sizeof(Elf64RelEntry)},
      {.tag = DynamicArrayTag::kRelCount, .value = 1},

      {.tag = DynamicArrayTag::kRela, .value = rela_table.value()},
      {.tag = DynamicArrayTag::kRelaSize, .value = sizeof(Elf64RelaEntry)},
      {.tag = DynamicArrayTag::kRelaCount, .value = 1},

      {.tag = DynamicArrayTag::kRelr, .value = relr_table.value()},
      {.tag = DynamicArrayTag::kRelrSize, .value = sizeof(uint64_t)},

      {.tag = DynamicArrayTag::kNull, .value = 0},
  };

  // Apply an offset of 0x100.
  Program program{writer.data(), LinkTimeAddr(0), RunTimeAddr(0x100)};
  ApplyDynamicRelocs(program, dynamic);

  // Expect that the data has been updated.
  EXPECT_EQ(program.ReadWord(offset1), 0x101u);  // patched from 0x1 -> 0x101
  EXPECT_EQ(program.ReadWord(offset2), 0x102u);  // patched from 0x2 -> 0x102
  EXPECT_EQ(program.ReadWord(offset3), 0x103u);  // patched from 0x3 -> 0x103
}

TEST(ApplyDynamicRelocs, RelCount) {
  // Create a fake "ELF program" with a rel section.
  BinaryWriter writer;

  // Write out a single program value.
  LinkTimeAddr first_word = writer.Write(uint64_t{1});

  // Write out a some rel entries, only the first of which is valid.
  LinkTimeAddr rel_table = writer.Write(Elf64RelEntry{
      .offset = LinkTimeAddr(0), .info = Elf64RelInfo::OfType(ElfRelocType::kRelative)});
  writer.Write(
      Elf64RelEntry{.offset = LinkTimeAddr(0), .info = Elf64RelInfo::OfType(ElfRelocType::kNone)});
  writer.Write(
      Elf64RelEntry{.offset = LinkTimeAddr(0), .info = Elf64RelInfo::OfType(ElfRelocType::kNone)});

  // Generate a dynamic table, with RelCount set to "1".
  Elf64DynamicEntry dynamic[] = {
      {.tag = DynamicArrayTag::kRel, .value = rel_table.value()},
      {.tag = DynamicArrayTag::kRelSize, .value = 3 * sizeof(Elf64RelEntry)},
      {.tag = DynamicArrayTag::kRelCount, .value = 1},

      {.tag = DynamicArrayTag::kNull, .value = 0},
  };

  // Apply an offset of 0x100.
  Program program{writer.data(), LinkTimeAddr(0), RunTimeAddr(0x100)};
  ApplyDynamicRelocs(program, dynamic);

  // Expect the value is updated, and only the first reloc was applied.
  EXPECT_EQ(program.ReadWord(first_word), 0x101u);  // patched from 0x1 -> 0x101
}

TEST(ApplyDynamicRelocs, RelaCount) {
  // Create a fake "ELF program" with a rela section.
  BinaryWriter writer;

  // Write out a program value.
  LinkTimeAddr first_word = writer.Write(uint64_t{1});

  // Write out a some rela entries, only the first of which is valid.
  LinkTimeAddr rela_table =
      writer.Write(Elf64RelaEntry{.offset = LinkTimeAddr(0),
                                  .info = Elf64RelInfo::OfType(ElfRelocType::kRelative),
                                  .addend = 1});
  writer.Write(
      Elf64RelaEntry{.offset = LinkTimeAddr(0), .info = Elf64RelInfo::OfType(ElfRelocType::kNone)});
  writer.Write(
      Elf64RelaEntry{.offset = LinkTimeAddr(0), .info = Elf64RelInfo::OfType(ElfRelocType::kNone)});

  // Generate a dynamic table, with RelaCount set to "1".
  Elf64DynamicEntry dynamic[] = {
      {.tag = DynamicArrayTag::kRela, .value = rela_table.value()},
      {.tag = DynamicArrayTag::kRelaSize, .value = 3 * sizeof(Elf64RelaEntry)},
      {.tag = DynamicArrayTag::kRelaCount, .value = 1},

      {.tag = DynamicArrayTag::kNull, .value = 0},
  };

  // Apply an offset of 0x100.
  Program program{writer.data(), LinkTimeAddr(0), RunTimeAddr(0x100)};
  ApplyDynamicRelocs(program, dynamic);

  // Expect the value is updated, and only the first reloc was applied.
  EXPECT_EQ(program.ReadWord(first_word), 0x101u);  // patched from 0x1 -> 0x101
}

TEST(ApplyDynamicRelocs, NonZeroLinkAddress) {
  // Create a fake "ELF program" with a rela, rel, and relr sections.
  BinaryWriter writer(LinkTimeAddr(0x1000));

  // Write out some program values.
  //
  // Each value refers to its own link address, assuming we were linked
  // at address 0x1000.
  LinkTimeAddr offset1 = writer.Write(uint64_t{0x1000});
  LinkTimeAddr offset2 = writer.Write(uint64_t{0x1008});
  LinkTimeAddr offset3 = writer.Write(uint64_t{0x1010});

  // Write out rel/rela/relr entries patching each of the three values.
  LinkTimeAddr rel_table = writer.Write(Elf64RelEntry{
      .offset = offset1,
      .info = Elf64RelInfo::OfType(ElfRelocType::kRelative),
  });
  LinkTimeAddr rela_table = writer.Write(Elf64RelaEntry{
      .offset = offset2, .info = Elf64RelInfo::OfType(ElfRelocType::kRelative), .addend = 0x1008});
  LinkTimeAddr relr_table = writer.Write(offset3);

  // Generate a dynamic table.
  Elf64DynamicEntry dynamic[] = {
      {.tag = DynamicArrayTag::kRel, .value = rel_table.value()},
      {.tag = DynamicArrayTag::kRelSize, .value = sizeof(Elf64RelEntry)},
      {.tag = DynamicArrayTag::kRelCount, .value = 1},

      {.tag = DynamicArrayTag::kRela, .value = rela_table.value()},
      {.tag = DynamicArrayTag::kRelaSize, .value = sizeof(Elf64RelaEntry)},
      {.tag = DynamicArrayTag::kRelaCount, .value = 1},

      {.tag = DynamicArrayTag::kRelr, .value = relr_table.value()},
      {.tag = DynamicArrayTag::kRelrSize, .value = sizeof(uint64_t)},

      {.tag = DynamicArrayTag::kNull, .value = 0},
  };

  // Load the program at address 0x2000.
  Program program{writer.data(), LinkTimeAddr(0x1000), RunTimeAddr(0x2000)};
  ApplyDynamicRelocs(program, dynamic);

  // Expect that the data has been updated.
  EXPECT_EQ(program.ReadWord(offset1), 0x2000u);
  EXPECT_EQ(program.ReadWord(offset2), 0x2008u);
  EXPECT_EQ(program.ReadWord(offset3), 0x2010u);
}

}  // namespace
}  // namespace static_pie
