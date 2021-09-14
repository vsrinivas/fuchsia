// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/layout.h>
#include <lib/elfldltl/relocation.h>

#include <limits>

#include <zxtest/zxtest.h>

namespace {

template <class... Elf>
struct TestAllFormats {
  template <typename Test>
  void OneTest(Test&& test) const {
    ASSERT_NO_FATAL_FAILURES((test(Elf{}), ...));
  }

  template <typename... Test>
  void operator()(Test&&... tests) const {
    ASSERT_NO_FATAL_FAILURES((OneTest(tests), ...));
  }
};

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

TEST(ElfldltlRelocationTests, VisitRelativeEmpty) {
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeEmpty);
}

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

constexpr auto VisitRelativeRel = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Rel = typename RelocInfo::Rel;

  constexpr Rel relocs[] = {
      {8, 0},
      {24, 0},
  };

  RelocInfo info;
  info.set_rel(relocs, 2);

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

TEST(ElfldltlRelocationTests, VisitRelativeRel) {
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRel);
}

constexpr auto VisitRelativeRela = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Rela = typename RelocInfo::Rela;

  constexpr Rela relocs[] = {
      {{8, 0}, 0x11111111},
      {{24, 0}, 0x33333333},
  };

  RelocInfo info;
  info.set_rela(relocs, 2);

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

TEST(ElfldltlRelocationTests, VisitRelativeRela) {
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRela);
}

constexpr auto VisitRelativeRelrSingle = [](auto elf) {
  using RelocInfo = elfldltl::RelocationInfo<decltype(elf)>;
  using Addr = typename RelocInfo::Addr;

  constexpr Addr relocs[] = {
      8,
  };

  RelocInfo info;
  info.set_relr(relocs);

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

TEST(ElfldltlRelocationTests, VisitRelativeRelrSingle) {
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRelrSingle);
}

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
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRelrNoBitmaps);
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
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRelrSingleBitmap);
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
  elfldltl::AllFormats<TestAllFormats>{}(VisitRelativeRelrMultipleBitmaps);
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

TEST(ElfldltlRelocationTests, VisitSymbolicEmpty) {
  elfldltl::AllFormats<TestAllFormats>{}(VisitSymbolicEmpty);
}

// TODO(fxbug.dev/72221): real VisitSymbolic tests

}  // namespace
