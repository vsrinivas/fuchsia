// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/layout.h>
#include <lib/elfldltl/machine.h>
#include <lib/elfldltl/relocation.h>

#include <limits>

#include "tests.h"

namespace {

constexpr auto VisitRelativeEmpty = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  RelocInfo info;
  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&count](auto&& reloc) -> bool {
    ++count;
    return false;
  }));
  EXPECT_EQ(0, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeEmpty) { TestAllFormats(VisitRelativeEmpty); }

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocOffset(const RelocInfo& info,
                                                    const typename RelocInfo::Rel& reloc) {
  return reloc.offset;
}

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocOffset(const RelocInfo& info,
                                                    const typename RelocInfo::Rela& reloc) {
  return reloc.offset;
}

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocOffset(const RelocInfo& info,
                                                    const typename RelocInfo::size_type& reloc) {
  return reloc;
}

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocAddend(const RelocInfo& info,
                                                    const typename RelocInfo::Rel& reloc) {
  return 0;
}

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocAddend(const RelocInfo& info,
                                                    const typename RelocInfo::Rela& reloc) {
  return reloc.addend;
}

template <class RelocInfo>
constexpr typename RelocInfo::size_type RelocAddend(const RelocInfo& info,
                                                    const typename RelocInfo::size_type& reloc) {
  return 0;
}

constexpr auto kTestMachine = elfldltl::ElfMachine::kNone;
using TestType = elfldltl::RelocationTraits<kTestMachine>::Type;
constexpr uint32_t kRelativeType = static_cast<uint32_t>(TestType::kRelative);

template <bool BadCount = false>
constexpr auto VisitRelativeRel = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Rel = typename RelocInfo::Rel;

  constexpr Rel relocs[] = {
      {8, kRelativeType},
      {24, kRelativeType},
  };

  RelocInfo info;
  info.set_rel(relocs, BadCount ? 99 : 2);

  EXPECT_TRUE(RelocInfo::template ValidateRelative<kTestMachine>(info.rel_relative()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    switch (count++) {
      case 0:
        EXPECT_EQ(8, offset);
        break;
      case 1:
        EXPECT_EQ(24, offset);
        break;
    }
    return true;
  }));
  EXPECT_EQ(2, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRel) { TestAllFormats(VisitRelativeRel<>); }

TEST(ElfldltlRelocationTests, VisitRelativeBadRelCount) { TestAllFormats(VisitRelativeRel<true>); }

template <bool BadCount = false>
constexpr auto VisitRelativeRela = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Rela = typename RelocInfo::Rela;

  constexpr Rela relocs[] = {
      {{8, kRelativeType}, 0x11111111},
      {{24, kRelativeType}, 0x33333333},
  };

  RelocInfo info;
  info.set_rela(relocs, BadCount ? 99 : 2);

  EXPECT_TRUE(RelocInfo::template ValidateRelative<kTestMachine>(info.rela_relative()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    auto addend = RelocAddend(info, reloc);
    switch (count++) {
      case 0:
        EXPECT_EQ(8, offset);
        EXPECT_EQ(0x11111111, addend);
        break;
      case 1:
        EXPECT_EQ(24, offset);
        EXPECT_EQ(0x33333333, addend);
        break;
    }
    return true;
  }));
  EXPECT_EQ(2, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRela) { TestAllFormats(VisitRelativeRela<>); }

TEST(ElfldltlRelocationTests, VisitRelativeBadRelaCount) {
  TestAllFormats(VisitRelativeRela<true>);
}

constexpr auto VisitRelativeRelrSingle = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Addr = typename RelocInfo::Addr;

  constexpr Addr relocs[] = {
      8,
  };

  RelocInfo info;
  info.set_relr(relocs);

  EXPECT_TRUE(RelocInfo::ValidateRelative(info.relr()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    switch (count++) {
      case 0:
        EXPECT_EQ(8, offset);
        break;
    }
    return true;
  }));
  EXPECT_EQ(1, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRelrSingle) { TestAllFormats(VisitRelativeRelrSingle); }

constexpr auto VisitRelativeRelrNoBitmaps = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Addr = typename RelocInfo::Addr;

  constexpr Addr relocs[] = {
      0x8,
      0x18,
      0x28,
  };

  RelocInfo info;
  info.set_relr(relocs);

  EXPECT_TRUE(RelocInfo::ValidateRelative(info.relr()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    switch (count++) {
      case 0:
        EXPECT_EQ(0x8, offset);
        break;
      case 1:
        EXPECT_EQ(0x18, offset);
        break;
      case 2:
        EXPECT_EQ(0x28, offset);
        break;
    }
    return true;
  }));
  EXPECT_EQ(3, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRelrNoBitmaps) {
  TestAllFormats(VisitRelativeRelrNoBitmaps);
}

constexpr auto VisitRelativeRelrSingleBitmap = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Addr = typename RelocInfo::Addr;

  constexpr Addr relocs[] = {
      0x8,
      0b10101,
  };

  RelocInfo info;
  info.set_relr(relocs);

  EXPECT_TRUE(RelocInfo::ValidateRelative(info.relr()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    EXPECT_EQ(0x8 + (sizeof(Addr) * 2 * count), offset);
    ++count;
    return true;
  }));
  EXPECT_EQ(3, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRelrSingleBitmap) {
  TestAllFormats(VisitRelativeRelrSingleBitmap);
}

constexpr auto VisitRelativeRelrMultipleBitmaps = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Addr = typename RelocInfo::Addr;
  using size_type = typename RelocInfo::size_type;

  constexpr auto bitmap = [](uint32_t bits) -> size_type {
    if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
      return bits;
    } else {
      return (static_cast<uint64_t>(bits) << 32) | bits;
    }
  };

  constexpr Addr relocs[] = {
      0x8,
      bitmap(0x55555555),
      bitmap(0xaaaaaaaa) | 1,
  };

  RelocInfo info;
  info.set_relr(relocs);

  EXPECT_TRUE(RelocInfo::ValidateRelative(info.relr()));

  size_t count = 0;
  EXPECT_TRUE(info.VisitRelative([&](auto&& reloc) -> bool {
    auto offset = RelocOffset(info, reloc);
    EXPECT_EQ(0x8 + (sizeof(Addr) * 2 * count), offset, "%zu * 2 * %zu", sizeof(Addr), count);
    ++count;
    return true;
  }));

  EXPECT_EQ(decltype(elf)::kAddressBits, count);
};

TEST(ElfldltlRelocationTests, VisitRelativeRelrMultipleBitmaps) {
  TestAllFormats(VisitRelativeRelrMultipleBitmaps);
}

constexpr auto VisitSymbolicEmpty = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  RelocInfo info;
  size_t count = 0;
  EXPECT_TRUE(info.VisitSymbolic([&count](auto&& reloc) -> bool {
    ++count;
    return false;
  }));
  EXPECT_EQ(0, count);
};

TEST(ElfldltlRelocationTests, VisitSymbolicEmpty) { TestAllFormats(VisitSymbolicEmpty); }

// TODO(fxbug.dev/72221): real VisitSymbolic tests

template <elfldltl::ElfMachine Machine>
constexpr void CheckMachine() {
  using Traits = elfldltl::RelocationTraits<Machine>;

  // Each machine must provide all these as distinct values, and no others.
  // This is mostly just a compile-time test to elicit errors if a Type::kFoo
  // is missing and to get the compiler warnings if any enum constants are
  // omitted from this switch.  The only runtime test is that kNone is zero.
  uint32_t type = 0;
  switch (static_cast<typename Traits::Type>(type)) {
    case Traits::Type::kNone:  // Has value zero on every machine.
      EXPECT_EQ(0u, type);
      break;

      // All other values are machine-dependent.
    case Traits::Type::kRelative:
    case Traits::Type::kAbsolute:
    case Traits::Type::kPlt:
    case Traits::Type::kTlsAbsolute:
    case Traits::Type::kTlsRelative:
    case Traits::Type::kTlsModule:
      FAIL();
      break;

    default:
      if (type == Traits::kGot) {
        FAIL();
        break;
      }

      if (type == Traits::kTlsDesc) {
        FAIL();
        break;
      }
  }
}

template <elfldltl::ElfMachine... Machines>
struct CheckMachines {
  CheckMachines() { (CheckMachine<Machines>(), ...); }
};

TEST(ElfldltlRelocationTests, Machines) { elfldltl::AllSupportedMachines<CheckMachines>(); }

}  // namespace
